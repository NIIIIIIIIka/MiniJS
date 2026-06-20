# QuickJS 架构理解指南

本文档用于帮助从 0 理解 [bellard/quickjs](https://github.com/bellard/quickjs) 的整体架构，并把它映射到你正在设计的 `MiniJSVM` 项目中。

QuickJS 不是一个“玩具解释器”，而是一个非常完整、紧凑、可嵌入的 JavaScript 引擎。它的难点不在于目录很大，而在于很多核心机制都压缩在少量 C 文件中：Parser、字节码编译器、VM、对象系统、闭包、模块、Promise、GC、RegExp、Unicode 等都高度集中。

## 1. QuickJS 是什么

QuickJS 的官方定位是：

```text
small and embeddable Javascript engine
```

它的主要特点：

- 使用 C 实现
- 代码量小，依赖少
- 支持接近完整的现代 ECMAScript
- 直接把 JavaScript 编译为字节码
- 使用栈式字节码虚拟机执行
- 使用引用计数 GC，并带循环引用清理
- 提供简单但完整的 C API
- 可以通过 `qjsc` 把 JavaScript 编译成 C 文件或可执行文件

如果把它压缩成一句话：

```text
QuickJS = Parser + Bytecode Compiler + Stack VM + Runtime + Object System + Reference Counting GC + C Embedding API
```

## 2. 仓库核心文件

QuickJS 的源码组织非常直接，核心大多在根目录。

```text
quickjs/
├── quickjs.c              # 引擎核心：parser、compiler、vm、runtime、object、gc
├── quickjs.h              # 对外 C API
├── quickjs-opcode.h       # 字节码 opcode 定义
├── quickjs-atom.h         # 内置 atom 定义
├── qjs.c                  # 命令行解释器
├── qjsc.c                 # 字节码/可执行文件编译器
├── quickjs-libc.c         # std/os 标准库与宿主能力绑定
├── quickjs-libc.h
├── libregexp.c            # 正则表达式引擎
├── libunicode.c           # Unicode 支持
├── cutils.c               # 通用工具
├── dtoa.c                 # number/string 转换
├── examples/              # 嵌入式使用示例
├── tests/                 # 测试
└── doc/quickjs.html       # 官方文档
```

最重要的阅读顺序不是从 `quickjs.c` 第一行开始硬读，而是：

```text
quickjs.h
    ↓
qjs.c
    ↓
qjsc.c
    ↓
quickjs-opcode.h
    ↓
quickjs.c 中的 Runtime / Value / Object / VM / Compiler
```

## 3. 一条 JavaScript 的执行路径

以：

```javascript
let a = 1 + 2;
print(a);
```

为例，QuickJS 大致经历：

```text
Source Code
    ↓
Lexer
    ↓
Parser
    ↓
Bytecode Compiler
    ↓
Function Bytecode
    ↓
VM Execute
    ↓
Runtime Object / Value / GC
```

注意一个关键点：QuickJS 官方文档明确说，它的编译器直接生成字节码，不保留类似 AST 的中间表示。这和你计划中的 MiniJSVM 可以不同。

对学习项目来说，更推荐：

```text
Source -> Lexer -> Parser -> AST -> AST Interpreter -> Bytecode -> VM
```

而 QuickJS 更接近：

```text
Source -> Parser/Compiler -> Bytecode -> VM
```

QuickJS 这样做的好处是快、内存少、工程紧凑；缺点是新手阅读门槛高。

## 4. 顶层模块关系

可以把 QuickJS 理解成下面几层：

```text
qjs / qjsc
    ↓
quickjs.h C API
    ↓
JSRuntime / JSContext
    ↓
Parser + Bytecode Compiler
    ↓
Stack-based VM
    ↓
JSValue / JSObject / JSString / JSAtom
    ↓
Reference Counting GC
```

其中：

- `qjs` 是命令行解释器
- `qjsc` 是命令行编译器
- `quickjs.h` 是嵌入者看到的 API
- `quickjs.c` 是引擎主体
- `quickjs-libc.c` 把宿主系统能力暴露给 JS

## 5. Runtime 与 Context

QuickJS 的两个最重要概念是：

```text
JSRuntime
JSContext
```

### JSRuntime

`JSRuntime` 表示一个 JavaScript 运行时实例，可以理解成：

```text
一个独立的 JS 堆 + GC 管理器 + Atom 表 + Class 表 + 运行时配置
```

特点：

- 一个进程中可以创建多个 `JSRuntime`
- 不同 `JSRuntime` 之间不能直接交换对象
- 一个 `JSRuntime` 对应一套对象堆
- QuickJS 在一个 runtime 内部不支持多线程同时执行 JS
- 内存限制、GC 阈值、最大栈大小等都挂在 runtime 上

你在 MiniJSVM 中可以设计为：

```cpp
class Runtime
{
    Heap heap;
    GC gc;
    AtomTable atoms;
};
```

### JSContext

`JSContext` 表示一个 JavaScript Realm，可以理解成：

```text
一套全局对象 + 内建对象 + 执行上下文
```

特点：

- 一个 `JSRuntime` 下可以有多个 `JSContext`
- 同一个 runtime 下的 context 可以共享对象
- 每个 context 有自己的 global object
- C API 大多数操作都需要传入 `JSContext*`

类比浏览器：

```text
JSRuntime ≈ 一个浏览器进程中的 JS 堆
JSContext ≈ 一个页面或 iframe 的全局环境
```

MiniJSVM 第一版可以先不区分 runtime/context，只做：

```cpp
class VM
{
    Runtime runtime;
    GlobalObject global;
};
```

等对象系统和模块系统稳定后，再拆分。

## 6. JSValue：所有值的统一表示

QuickJS 用 `JSValue` 表示所有 JavaScript 值：

```text
undefined
null
boolean
int
float64
string
symbol
object
function
module
bigint
exception
```

官方文档里提到：

- 32 位平台使用 NaN boxing
- 64 位平台中 `JSValue` 是 128-bit
- 32-bit int 和引用计数对象都有优化路径
- `JSValue` 正好适合用两个 CPU 寄存器返回

可以粗略理解为：

```cpp
struct JSValue
{
    Tag tag;
    union
    {
        int32_t int_value;
        double float_value;
        JSObject* object;
        JSString* string;
    };
};
```

QuickJS 中所有带引用计数的 tag 都是负数，这方便快速判断某个值是否需要 `JS_DupValue()` / `JS_FreeValue()`。

MiniJSVM 可以先做简单版：

```cpp
enum class ValueType
{
    Undefined,
    Null,
    Boolean,
    Number,
    Object,
    Function,
    String
};

class Value
{
    ValueType type;
};
```

后续再考虑：

- int / double 分离
- string interning
- object reference count
- NaN boxing

## 7. 字节码与 VM

QuickJS 的执行核心是字节码虚拟机。

官方文档给出的几个关键信息：

- 编译器直接生成字节码
- 字节码是 stack-based
- 生成字节码后会做若干优化
- 每个函数的最大栈大小在编译期计算
- 调试信息有独立压缩的行号表
- 闭包变量访问被优化到接近局部变量

### 栈式 VM

示例：

```javascript
1 + 2
```

可以理解为：

```text
push_i32 1
push_i32 2
add
return
```

VM 执行模型：

```cpp
while (true)
{
    Opcode op = read_opcode();

    switch (op)
    {
    case OP_push_i32:
        stack.push(read_i32());
        break;

    case OP_add:
        rhs = stack.pop();
        lhs = stack.pop();
        stack.push(add(lhs, rhs));
        break;

    case OP_return:
        return stack.pop();
    }
}
```

QuickJS 选择栈式 VM 的原因很朴素：

```text
simple and compact
```

这对你的 MiniJSVM 非常重要。第一版不要做寄存器 VM，栈式 VM 更适合理解编译器和解释器。

## 8. qjs 与 qjsc

QuickJS 有两个主要命令行程序。

### qjs

`qjs` 是解释器和 REPL。

职责：

- 创建 `JSRuntime`
- 创建 `JSContext`
- 初始化标准库
- 读取 JS 文件或命令行表达式
- 调用 `JS_Eval()`
- 处理异常、Promise job、退出码

你可以把它理解成：

```text
qjs.c = QuickJS 的 main.cpp
```

### qjsc

`qjsc` 是编译器。

职责：

- 读取 JS 文件
- 编译为 QuickJS bytecode
- 把 bytecode 序列化进 C 源文件
- 可选生成 `main()` 函数
- 调用系统 C 编译器生成可执行文件

执行链路：

```text
demo.js
    ↓
qjsc
    ↓
demo.c
    ↓
gcc / clang
    ↓
demo executable
```

这也是 QuickJS 很有特色的地方：它不是 JIT，而是把 JS 编译成 QuickJS 字节码，再把字节码嵌入 C 程序。

MiniJSVM 后期可以模仿一个小工具：

```bash
minijsc demo.js -o demo.bc
minijs demo.bc
```

或者：

```bash
minijsc demo.js --dump-bytecode
```

## 9. 对象系统

QuickJS 的对象系统核心包括：

```text
JSObject
JSShape
JSProperty
JSAtom
JSClass
```

### JSObject

对象本身保存：

- class id
- prototype
- shape
- property storage
- 特殊对象数据，例如 Array、Function、Promise、RegExp

可以先理解为：

```cpp
class JSObject
{
    JSShape* shape;
    std::vector<JSProperty> props;
};
```

### JSShape

官方文档提到：对象的 shape 会在多个对象之间共享，用来节省内存。

Shape 可以理解为：

```text
对象的结构描述：
- prototype
- property name
- property flags
- property layout
```

例如：

```javascript
let a = { x: 1, y: 2 };
let b = { x: 3, y: 4 };
```

`a` 和 `b` 的值不同，但结构一样，可以共享 shape：

```text
Shape:
    x
    y

a.props = [1, 2]
b.props = [3, 4]
```

MiniJSVM 初期不要做 shape，直接用：

```cpp
std::unordered_map<std::string, Value>
```

等对象系统跑通后，再升级成：

```text
Atom -> Property -> Shape -> Object Storage
```

### JSClass

QuickJS 的 `JSClass` 主要用于：

- 给 C 扩展对象绑定类型
- 添加 finalizer
- 添加 gc_mark
- 定义 exotic object behavior
- 支持宿主对象，例如文件、模块、特殊内建对象

对 MiniJSVM 来说，第一版可以只做：

```cpp
enum class ObjectKind
{
    Ordinary,
    Function,
    Array,
    NativeFunction
};
```

## 10. Atom：属性名的整数化

QuickJS 使用 `JSAtom` 存储对象属性名和部分字符串。

Atom 的本质是：

```text
interned string -> uint32_t id
```

例如：

```javascript
obj.name
obj.age
```

内部可以变成：

```text
"name" -> 1024
"age"  -> 1025
```

这样做的好处：

- 属性名比较变成整数比较
- 相同字符串只存一份
- 对象 shape 更容易共享
- 运行时内存更小

MiniJSVM 可以按三个阶段实现：

```text
V0: std::string 直接做 key
V1: StringPool 去重
V2: AtomTable，把字符串映射成 uint32_t
```

## 11. 字符串、数字与数组优化

QuickJS 在很多地方做了“小而有效”的优化。

### 字符串

字符串内部可能用：

```text
8-bit char array
16-bit char array
```

如果字符串全是 ASCII 或 Latin-1，就不用 16-bit 存储，从而省内存。随机访问字符仍然很快。

### 数字

JavaScript 语义上主要是 Number，但 QuickJS 内部区分：

```text
int32 fast path
float64 fallback
```

例如：

```javascript
1 + 2
```

可以走 int32 快路径。

```javascript
1.5 + 2.3
```

再走 float64。

### 数组

QuickJS 对没有中间空洞的数组有优化。

例如：

```javascript
let a = [1, 2, 3];
```

比下面这种更容易优化：

```javascript
let a = [];
a[100000] = 1;
```

MiniJSVM 初期可以只做普通 vector：

```cpp
class JSArray : public JSObject
{
    std::vector<Value> elements;
};
```

后面再考虑稀疏数组。

## 12. GC：引用计数 + 循环引用清理

QuickJS 的 GC 不是传统浏览器 JS 引擎常见的 tracing GC，而是：

```text
Reference Counting + Cycle Removal
```

### 引用计数

每个需要管理生命周期的对象都有引用计数。

当新增引用：

```cpp
JS_DupValue(ctx, value);
```

当释放引用：

```cpp
JS_FreeValue(ctx, value);
```

如果引用计数归零，对象立即释放。

优点：

- 行为确定
- 内存释放及时
- 嵌入式场景友好
- C API 调用者容易理解对象所有权

缺点：

- 每次赋值可能要更新引用计数
- 循环引用无法靠普通引用计数释放

### 循环引用清理

例如：

```javascript
let a = {};
let b = {};
a.b = b;
b.a = a;
```

即使外部变量不再引用 `a` 和 `b`，它们内部仍互相引用，普通引用计数无法释放。

QuickJS 会在内存增长到一定程度时运行 cycle removal pass。官方文档强调，这个算法只依赖引用计数和对象内容，C 代码不需要手动维护显式 GC roots。

MiniJSVM 的推荐路线：

```text
V0: 手动 delete，先跑通解释器
V1: 引用计数
V2: 作用域退出自动 release
V3: 对象属性 release
V4: cycle detector
```

## 13. 函数调用与闭包

QuickJS 的函数调用做了大量优化。官方文档提到：JavaScript 参数和局部变量放在系统栈上。

可以把函数调用理解为：

```text
JSFunctionBytecode
    ↓
Call Frame
    ↓
Arguments + Local Variables + Operand Stack
```

闭包变量需要特殊处理：

```javascript
function outer() {
    let x = 1;
    return function inner() {
        return x;
    };
}
```

`x` 本来是 `outer` 的局部变量，但 `outer` 返回后 `inner` 仍然需要访问它，所以它必须从普通局部变量提升为可被闭包持有的变量。

MiniJSVM 可以分两步：

```text
V0: 函数调用，不支持闭包
V1: 函数对象保存声明时 Environment*
V2: Upvalue / Closure Object
```

QuickJS 已经做到了“闭包变量访问接近局部变量”的优化，但学习项目不要一开始追这个。

## 14. 异常机制

QuickJS 的 C API 中，异常不是用 C++ exception，而是通过特殊 `JSValue` 表示：

```text
JS_EXCEPTION
```

当 C API 返回 `JS_EXCEPTION` 时，真正的异常对象存在 `JSContext` 里，需要用：

```cpp
JS_GetException(ctx)
```

取出来。

这说明 QuickJS 的异常机制本质是：

```text
返回特殊值 + context 保存异常状态
```

MiniJSVM 初期可以用 C++ 异常简化：

```cpp
throw RuntimeError("xxx");
```

但如果你想更接近 QuickJS，可以实现：

```cpp
class Context
{
    Value currentException;
};

Value eval(...)
{
    if (error)
        return Value::Exception();
}
```

## 15. 标准库与宿主绑定

QuickJS 核心引擎和宿主能力是分开的。

```text
quickjs.c       -> JS engine core
quickjs-libc.c  -> std/os module, file, process, timer, worker
qjs.c           -> command line integration
```

这点非常值得你在 MiniJSVM 中学习。

不要把 `print()`、文件 IO、时间函数直接塞进 VM 核心。更好的方式：

```text
Runtime Core
    只负责语言语义

Native Binding Layer
    注册 print、clock、readFile 等宿主函数
```

示例设计：

```cpp
using NativeFn = Value (*)(VM& vm, int argc, Value* argv);

global.define("print", Value::nativeFunction(native_print));
```

## 16. 模块系统

QuickJS 支持 ES6 modules。

默认解析规则大致是：

- 以 `.` 或 `..` 开头的是相对路径模块
- 不以 `.` 或 `..` 开头的是系统模块，例如 `std`、`os`
- `.so` 结尾的可以是 native module

MiniJSVM 第一版不建议做完整模块系统。

推荐路线：

```text
V0: 单文件执行
V1: load("file.js")
V2: CommonJS 风格 require()
V3: 简化版 ES module
```

## 17. RegExp、Unicode、BigInt

QuickJS 自己实现了：

- RegExp 引擎
- Unicode 库
- BigInt

这些都非常有技术含量，但不适合作为 MiniJSVM 的前期目标。

建议优先级：

```text
先跳过：
- RegExp
- full Unicode
- BigInt
- Intl
- Proxy
- Promise
- async/await
```

等主线完成后再挑一个扩展即可。

## 18. 从 0 阅读 QuickJS 的路线

### 第一步：先看外壳

读：

```text
qjs.c
quickjs.h
```

目标：

- QuickJS 如何创建 runtime/context
- 如何执行一个 JS 文件
- 如何处理异常
- 如何初始化标准库

重点 API：

```cpp
JS_NewRuntime()
JS_NewContext()
JS_Eval()
JS_FreeContext()
JS_FreeRuntime()
JS_GetException()
```

### 第二步：看值系统

读：

```text
quickjs.h
quickjs.c
```

目标：

- `JSValue` 如何表示不同类型
- tag 是如何设计的
- 哪些值需要引用计数
- `JS_DupValue` / `JS_FreeValue` 的责任边界

重点概念：

```text
JS_TAG_INT
JS_TAG_FLOAT64
JS_TAG_STRING
JS_TAG_OBJECT
JS_TAG_FUNCTION_BYTECODE
JS_TAG_EXCEPTION
```

### 第三步：看对象系统

读 `quickjs.c` 中和下面关键词相关的代码：

```text
JSObject
JSShape
JSProperty
JSAtom
JSClass
```

目标：

- 普通对象如何保存属性
- prototype 如何工作
- shape 如何共享
- native class 如何挂 finalizer

### 第四步：看字节码

读：

```text
quickjs-opcode.h
quickjs.c
```

搜索：

```text
OP_
JSFunctionBytecode
```

目标：

- opcode 有哪些
- bytecode 如何存储
- 函数如何持有字节码
- 常量池、变量、闭包信息如何保存

### 第五步：看 VM 执行循环

在 `quickjs.c` 中搜索：

```text
JS_CallInternal
```

或 opcode dispatch 相关代码。

目标：

- VM 如何取指
- operand stack 如何变化
- 函数调用如何进入新 frame
- `return` 如何回到调用者
- 异常如何中断执行

### 第六步：看 Parser/Compiler

这是最难的一部分，建议最后看。

目标：

- JavaScript 语法如何被解析
- 作用域如何分析
- 变量如何分配 slot
- 表达式如何生成 bytecode
- 函数如何生成 `JSFunctionBytecode`

对 MiniJSVM 来说，不需要完全复刻 QuickJS 的 parser。你可以先用 AST 过渡。

## 19. QuickJS 到 MiniJSVM 的映射

| QuickJS | MiniJSVM 第一版 |
| --- | --- |
| `JSRuntime` | `Runtime` / `Heap` |
| `JSContext` | `VM` / `GlobalEnv` |
| `JSValue` | `Value` |
| `JSObject` | `Object` |
| `JSAtom` | 先用 `std::string`，后续做 `AtomTable` |
| `JSShape` | 先跳过，后续优化对象布局 |
| `JSFunctionBytecode` | `FunctionObject + Chunk` |
| `quickjs-opcode.h` | `opcode.h` |
| `JS_DupValue` | `Value::retain()` |
| `JS_FreeValue` | `Value::release()` |
| `qjs.c` | `main.cpp` |
| `qjsc.c` | 后续 `minijsc` |
| `quickjs-libc.c` | `native/` 或 `builtin/` |

## 20. 你应该学习 QuickJS 的哪些设计

优先学习：

- `JSRuntime` / `JSContext` 的分层
- `JSValue` 统一值表示
- 栈式字节码 VM
- 函数字节码对象
- 引用计数所有权模型
- 对象属性与 Atom
- 标准库和核心引擎分离
- `qjs` 与 `qjsc` 的工具边界

暂时不要学：

- 完整 ES grammar
- async/await
- Promise job queue
- Proxy exotic behavior
- Generator
- RegExp engine
- BigInt
- Unicode normalization
- Test262 兼容细节

## 21. 推荐 MiniJSVM 实现顺序

基于 QuickJS 架构，但降低复杂度：

```text
1. Token / Lexer
2. Parser / AST
3. AST Interpreter
4. Value
5. Environment
6. FunctionObject
7. Bytecode Chunk
8. Stack VM
9. Object
10. Array
11. NativeFunction
12. Reference Counting
13. AtomTable
14. Closure
15. Module
```

对应 QuickJS 的阅读顺序：

```text
qjs.c
    ↓
quickjs.h
    ↓
JSValue
    ↓
JSRuntime / JSContext
    ↓
Object / Atom
    ↓
Opcode / VM
    ↓
Compiler
    ↓
GC
```

## 22. 总结

QuickJS 的核心架构可以理解为：

```text
一个小型可嵌入 JavaScript 引擎：

Source
  -> Direct Bytecode Compiler
  -> Stack VM
  -> JSValue Runtime
  -> Object System
  -> Reference Counting GC
  -> C API / Native Modules
```

它对 MiniJSVM 最有价值的启发不是“照着写完整 JavaScript”，而是：

- 引擎核心要小
- API 边界要清晰
- VM 用栈式模型先跑通
- 值系统统一表示
- 对象和属性名要逐步优化
- GC 可以从引用计数开始
- 标准库不要污染 VM 核心

如果你能把这些思想用 C++ 重新实现一遍，即使只支持 JavaScript 的子集，也已经是一个非常扎实的解释器/虚拟机项目。

## 参考资料

- QuickJS GitHub: <https://github.com/bellard/quickjs>
- QuickJS 官方文档: <https://bellard.org/quickjs/quickjs.html>
- QuickJS 官网: <https://bellard.org/quickjs/>

