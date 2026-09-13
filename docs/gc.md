# GC 第一阶段实现说明

本文记录 MiniJS bytecode VM 第一阶段 GC 的实现状态、对象图、根集合和后续边界。

这一阶段的目标不是立刻删除所有旧的 `std::shared_ptr` 兼容结构，而是先让 VM 内部主要运行时对象进入 VM 自己管理的 GC 堆，并跑通基础 mark-sweep。

## 当前目标

第一阶段 GC 的目标是：

- VM 内部运行时对象由 `Obj` 家族统一管理。
- GC 对象通过 `VM::allocateObject()` 分配，并挂入 `objects_` 链表。
- `collectGarbage()` 从根集合出发标记对象，再清扫不可达对象。
- `VM::run()` 对外仍通过 `copyOutValue()` 返回旧兼容值，避免一次性改动测试和外部接口。

## 代码位置

- GC 对象基类和类型枚举：[include/minijs/object.h](../include/minijs/object.h)
- VM GC 对象结构：[include/minijs/gc_object.h](../include/minijs/gc_object.h)
- GC 对象构造函数：[src/gc_object.cpp](../src/gc_object.cpp)
- GC 堆字段、分配模板和临时根：[include/minijs/vm.h](../include/minijs/vm.h) 中的 `allocateObject()`、`allocateInternalObject()`、`TemporaryRootScope`
- 标记、清扫和阈值逻辑：[src/vm.cpp](../src/vm.cpp) 中的 `markRoots()`、`markValue()`、`markObject()`、`markObjectChildren()`、`sweep()`、`collectGarbage()`、`collectGarbageIfNeeded()`
- 返回值兼容层：[src/vm.cpp](../src/vm.cpp) 中的 `copyOutValue()`、`copyOutClosure()`、`copyOutClass()`
- GC 回归测试：[tests/test_bytecode.cpp](../tests/test_bytecode.cpp)

## GC 对象

当前已经进入 GC 堆的对象包括：

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

这些对象都继承自 `Obj`，并通过 `ObjType` 区分类型。

## 对象所有权

所有 GC 对象都通过：

```cpp
VM::allocateObject<T>(...)
```

分配。分配后对象会挂到 `VM::objects_` 链表头部：

```text
objects_ -> Obj -> Obj -> Obj -> ...
```

`sweep()` 会遍历这条链表，释放没有被标记到的对象。

## 根集合

`markRoots()` 当前会标记这些根：

```text
stack_
globals_
openUpvalues_
frames_
temporaryRoots_
```

含义如下：

| 根 | 说明 |
| --- | --- |
| `stack_` | 操作数栈，同时保存参数、局部变量和临时值 |
| `globals_` | 全局变量和 VM 构造期创建的 builtin |
| `openUpvalues_` | 仍指向栈槽的 open upvalue |
| `frames_` | 当前调用栈中的 closure |
| `temporaryRoots_` | 分配中间对象时的临时保护根 |

## 标记阶段

标记入口主要有两个：

```cpp
markValue(const Value& value)
markObject(Obj* object)
```

`markValue()` 负责从 `Value` 进入对象图。如果 `Value` 持有 GC 对象指针，就调用 `markObject()`。

`markObject()` 会设置对象的 `marked` 标志，并调用：

```cpp
markObjectChildren(Obj* object)
```

继续标记子引用。

当前对象子引用关系如下：

| 对象 | 标记内容 |
| --- | --- |
| `ObjString` | 无子引用 |
| `ObjArray` | `elements` 中的每个 `Value` |
| `ObjObject` | `properties` 中的每个属性值 |
| `ObjFunction` | `chunk.constants()` 中的每个常量 |
| `ObjClosure` | 持有的 `ObjFunction` 和所有 `ObjUpvalue` |
| `ObjUpvalue` | closed 状态标记 `closed`；open 状态标记对应栈槽 |
| `ObjClass` | superclass、methods、staticMethods |
| `ObjInstance` | klass 和 fields |
| `ObjBoundMethod` | receiver 和 method closure |
| `ObjNativeFunction` | 无子引用 |

## 清扫阶段

`sweep()` 遍历 `objects_` 链表：

- 如果对象已标记，清除 `marked`，保留对象。
- 如果对象未标记，从链表摘除并 `delete`。

`heapObjectCount_` 表示 GC 堆中当前对象总数，清扫删除对象时同步递减。

## 临时根

`temporaryRoots_` 用来保护“已经分配，但还没有挂到稳定根上的对象”。

典型场景：

- 构造 `ObjClosure` 时，先分配出的 `ObjFunction` 需要临时保护。
- 构造 `keys()` 返回数组时，数组元素里的 `ObjString` 需要先临时保护。

现在临时根通过 RAII 封装：

```cpp
TemporaryRootScope roots(*this);
roots.add(object);
```

作用域退出时会自动恢复 `temporaryRoots_` 到进入作用域前的长度，避免异常路径留下临时根。

## 自动 GC 阈值

`collectGarbageIfNeeded()` 在分配对象前检查：

```cpp
heapObjectCount_ + 1 <= nextGcObjectCount_
```

如果超过阈值，就调用 `collectGarbage()`。

`collectGarbage()` 在 mark/sweep 后统一更新下一次触发阈值：

```cpp
nextGcObjectCount_ = std::max<std::size_t>(heapObjectCount_ * 2, 8);
```

这样手动 GC 和自动 GC 的阈值更新语义一致。

## objectCount()

`heapObjectCount_` 是 GC 堆里的全部对象数。

`builtinObjectCount_` 是 VM 构造期创建的 builtin native function 数量。builtin 是永久全局根，不属于用户程序创建的对象。

对测试暴露的：

```cpp
VM::objectCount()
```

返回：

```cpp
heapObjectCount_ - builtinObjectCount_
```

也就是“用户程序产生的 GC 对象数量”。

## VM 内部对象模型

当前 VM 内部运行时路径已经基本收敛到 GC 对象：

```text
String -> ObjString
Array -> ObjArray
Object -> ObjObject
Function template -> ObjFunction
Closure -> ObjClosure
Upvalue -> ObjUpvalue
Class -> ObjClass
Instance -> ObjInstance
Bound method -> ObjBoundMethod
Native function -> ObjNativeFunction
```

例如：

- `OP_CLOSURE` 从常量池里的 `BytecodeFunction` 模板构造 `ObjFunction + ObjClosure`。
- 函数调用只调用 `ObjClosure`。
- 方法绑定只使用 `ObjBoundMethod`。
- 数组、对象、字符串运行时操作只走对应 `Obj*`。

## 兼容层

当前仍然保留旧兼容结构：

```text
BytecodeClosure
BytecodeClass
BytecodeInstance
BytecodeBoundMethod
Array / Object / String 旧 Value 形态
```

它们主要用于：

- `copyOutValue()` 把 VM 内部 GC 对象返回给外部测试。
- `markValue()` 标记旧兼容值中的引用。

也就是说，当前边界是：

```text
VM 内部运行时：Obj*
VM 对外返回：旧兼容 Value
```

后续如果要彻底删除旧兼容层，需要同步调整 `VM::run()` 返回值和测试断言。

## 当前测试覆盖

现有测试覆盖了：

- 字符串、数组、对象、实例、类、闭包、upvalue、绑定方法的可达标记。
- 不可达对象的手动回收。
- 自动 GC 压力测试。
- `keys()` 临时根回收。
- 手动 `collectGarbage()` 幂等性。
- builtin native function 进入 GC 后仍由全局根保活。

## 后续工作

第一阶段之后可以继续做：

- 删除旧 `Bytecode*` 运行时兼容结构。
- 让 `VM::run()` 直接返回 GC Value 或设计更明确的外部值边界。
- 字符串 interning。
- GC trace/debug 日志。
- 更细的自动 GC 阈值策略。
- 更大规模的压力测试。
- AST interpreter 是否也迁入同一套 GC。
