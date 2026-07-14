# MiniJS GC 设计草案

本文档记录 MiniJS 后续引入垃圾回收时的对象图、根集合和分阶段落地路线。

当前目标不是立刻替换所有 `std::shared_ptr`，而是先把内存模型设计清楚。后续实现 GC 时，应优先服务 bytecode VM；AST 解释器可以暂时继续使用现有的 `shared_ptr` 模型。

## 为什么要引入GC

### 当前内存模型

MiniJS 目前主要依赖 C++ 智能指针维护引用型运行时值：

```cpp
std::shared_ptr<BytecodeClosure>
std::shared_ptr<BytecodeClass>
std::shared_ptr<BytecodeInstance>
std::shared_ptr<BytecodeBoundMethod>
std::shared_ptr<std::vector<Value>>
std::shared_ptr<std::unordered_map<std::string, Value>>
```

这种方式让数组、对象、类、实例和闭包都具备引用语义：

```javascript
let a = {};
let b = a;
b.name = "Tom";
a.name; // "Tom"
```

它的好处是实现简单，当前阶段可以专注语言语义。但它不是语言自己的内存系统，也不适合长期作为 MiniJS VM 的最终方案。

### 主要问题有两个：

1. **循环引用无法回收**：对象间互相引用会导致 `shared_ptr` 引用计数永远不为零，造成内存泄漏。
2. VM 无法控制分配、触发回收、统计内存和调试对象生命周期。

典型循环引用：

```javascript
let a = {};
let b = {};
a.b = b;
b.a = a;
```

如果未来对象全部由 VM 自己分配，这组值离开作用域后应该由 GC 回收，而不是依赖 C++ 引用计数。

## 核心设计思路

### 1. 需要管理的对象类型

第一版 GC 不需要一次性迁移所有类型，但最终应把堆对象统一成 `Obj` 家族。

目标对象类型：

```text
ObjString
ObjArray
ObjObject
ObjFunction
ObjClosure
ObjUpvalue
ObjClass
ObjInstance
ObjBoundMethod
ObjNativeFunction
```

### 2. 对象模型统一

可以采用类似下面的基础结构：

```cpp
enum class ObjType {
  String,
  Array,
  Object,
  Function,
  Closure,
  Upvalue,
  Class,
  Instance,
  BoundMethod,
  NativeFunction,
};
```

所有堆对象继承自 `Obj` 基类，由 VM 统一管理：

```cpp
struct Obj {
  ObjType type;        // 对象类型（字符串、数组、闭包等）
  bool marked = false; // GC 标记位
  Obj* next = nullptr; // 全局对象链表指针
};
```

VM 持有所有已分配对象的链表：

```cpp
class VM {
  Obj* objects_ = nullptr;
};
```

每次分配对象时，把对象挂到 `objects_` 链表。GC 的 `sweep()` 阶段沿着这条链表释放没有被标记的对象。

### 3. 根集合（Root Set）

标记-清除 GC 的第一步是从 root 出发。root 是“当前程序还能直接访问到的值”。

bytecode VM 的 root 至少包括：

```text
VM::stack_
VM::globals_
VM::frames_
VM::openUpvalues_
当前正在运行的 script closure
当前调用帧里的 closure
```

其中：

| 根来源              | 说明                                                       |
| :------------------ | :--------------------------------------------------------- |
| `VM::stack_`        | 操作数栈、局部变量、函数参数、临时值                       |
| `VM::globals_`      | 全局变量、内置函数                                         |
| `VM::frames_`       | 调用帧（CallFrame），包含当前运行的闭包（BytecodeClosure） |
| `VM::openUpvalues_` | 指向栈槽的 upvalue                                         |
| 常量池              | `BytecodeFunction::chunk.constants` 中引用的对象           |

常量池中的 `Value` 可能引用字符串、函数模板、数组名表等对象。只要某个 `ObjFunction` 或 `ObjClosure` 还活着，它引用的 chunk constants 也必须被标记。

AST 解释器侧也有 root：

```text
Interpreter::global_environment_
Interpreter::environment_
Environment 链
Interpreter::lastValue_
```

但第一版 GC 建议不覆盖 AST 解释器，避免同时迁移两套运行时。解释器可以继续依赖 `shared_ptr`，GC 先服务 bytecode VM。

## 对象引用关系

下面这张表是后续 `blackenObject()` 的实现依据。

```text
ObjArray:
  -> elements: Value[]

ObjObject:
  -> properties: map<string, Value>

ObjFunction:
  -> name: ObjString
  -> params: ObjString[]
  -> chunk.constants: Value[]
  -> upvalue descriptors

ObjClosure:
  -> function: ObjFunction
  -> upvalues: ObjUpvalue[]

ObjUpvalue:
  -> open stack slot 或 closed Value

ObjClass:
  -> name: ObjString
  -> superclass: ObjClass
  -> methods: map<string, ObjClosure>
  -> staticMethods: map<string, ObjClosure>

ObjInstance:
  -> klass: ObjClass
  -> fields: map<string, Value>

ObjBoundMethod:
  -> receiver: ObjInstance
  -> method: ObjClosure

ObjNativeFunction:
  -> name: ObjString
  -> C++ native function handle
```

标记时要遵循一个原则：

```text
标记 Value 时，如果 Value 持有 Obj 指针，就标记该 Obj。
标记 Obj 时，根据 ObjType 继续标记它引用的其它 Value 或 Obj。
```

每种对象在 GC 标记阶段需要递归标记的内容：

| 对象类型         | 需要标记的内容                             |
| :--------------- | :----------------------------------------- |
| `ObjArray`       | 数组元素（`Value[]`）                      |
| `ObjObject`      | 属性表（`map<string, Value>`）             |
| `ObjFunction`    | 函数名、参数、常量池                       |
| `ObjClosure`     | 引用的 `ObjFunction` 和 `ObjUpvalue[]`     |
| `ObjUpvalue`     | open 状态指向栈槽；closed 状态标记持有的值 |
| `ObjClass`       | 类名、父类、方法表、静态方法               |
| `ObjInstance`    | 类指针、实例字段                           |
| `ObjBoundMethod` | 接收者对象、绑定的方法                     |

## 关键生命周期

### Closure 和 Upvalue

闭包是 GC 的重点，因为它连接了函数模板、捕获变量和调用帧。

```text
ObjClosure
  -> ObjFunction
  -> ObjUpvalue[]
```

`ObjUpvalue` 有两种状态：

```text
open:
  指向 VM::stack_ 中的某个 slot。
  这个阶段真正的值由 stack_ root 标记。

closed:
  持有 closed Value。栈上的值拷贝到 Upvalue 内部
  这个阶段必须由 ObjUpvalue 自己继续标记 closed Value。
```

### Class 和 Instance

类和实例会形成比较长的引用链：

```text
ObjInstance
  -> ObjClass
      -> superclass
      -> methods
      -> staticMethods
```

实例字段也可以反向引用实例自身或其它对象：

```javascript
let box = Box();
box.self = box;
```

因此实例字段必须由 GC 追踪，不能用简单引用计数作为最终方案。

## 第一版 GC 路线

建议分阶段推进，不要一次性重写整个 `Value`。

### 阶段 1：设计文档

完成本文档，确认：

```text
哪些对象未来由 GC 管
从哪些 root 出发标记
每种对象会引用哪些其它值
第一版只覆盖 bytecode VM
AST 解释器暂时继续使用 shared_ptr
```

### 阶段 2：引入 Obj 基类和对象链表

新增：

```text
include/minijs/object.h
src/object.cpp
```

并在 VM 中新增：

```cpp
Obj* objects_ = nullptr;
```

先只提供分配和释放框架，不急着迁移所有对象。

### 阶段 3：先迁移 bytecode class 相关对象

优先迁移：

```text
BytecodeClosure
BytecodeClass
BytecodeInstance
BytecodeBoundMethod
Upvalue
```

原因是 class、closure、upvalue 已经形成复杂对象图，最能验证 GC 设计是否正确。

### 阶段 4：实现 mark-sweep

最小接口：

```cpp
void collectGarbage();
void markRoots();
void markValue(const Value& value);
void markObject(Obj* object);
void traceReferences();
void blackenObject(Obj* object);
void sweep();
```

基础流程：

```text
markRoots()
  -> traceReferences()
  -> sweep()
```

### 阶段 5：迁移数组、对象和字符串

当 class/closure 体系稳定后，再迁移：

```text
Array
Object
String
NativeFunction
```

字符串可以稍后考虑 intern 表。第一版不需要字符串驻留，先保证对象生命周期正确。

## 暂不处理的内容

第一版 GC 暂不做：

```text
分代 GC
增量 GC
压缩 GC
弱引用
字符串驻留
多线程安全
AST 解释器迁移
```

这些都可以等 mark-sweep 跑通后再考虑。

## 完成标准

开始写 GC 代码前，应该能明确回答：

1. 哪些 `ValueType` 会变成 GC 对象？
2. VM 从哪些 root 开始标记？
3. 每种对象在 `blackenObject()` 中要继续标记什么？
4. open upvalue 和 closed upvalue 的标记规则有什么不同？
5. 哪些 `shared_ptr` 会先保留，哪些会优先替换？

只要这五个问题答案稳定，就可以进入第一版 GC 实现。
