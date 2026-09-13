# MiniJS 运行时设计

本文档记录 MiniJS 当前运行时设计。默认 CLI 路径已经是 bytecode VM，`--interp` 保留 AST Interpreter 作为对照和回归路径。

## 执行流程

默认执行流程：

```text
Source Code
    -> Lexer
    -> Parser
    -> AST
    -> Bytecode Compiler
    -> Stack-based VM
    -> Runtime Value
```

调试和对照路径：

```text
Source Code
    -> Lexer
    -> Parser
    -> AST
    -> AST Interpreter
    -> Runtime Value
```

命令行中不带选项或使用 `--run` 会走 VM；`--interp` 会走 AST Interpreter；`--tokens`、`--ast` 和 `--bytecode` 分别用于观察前端和编译结果。

## 代码位置

- CLI 执行入口和模式分发：[src/main.cpp](../src/main.cpp)
- AST Interpreter 执行路径：[include/minijs/interpreter.h](../include/minijs/interpreter.h)、[src/interpreter.cpp](../src/interpreter.cpp)、[include/minijs/environment.h](../include/minijs/environment.h)
- Bytecode 编译和 Chunk：[include/minijs/compiler.h](../include/minijs/compiler.h)、[src/compiler.cpp](../src/compiler.cpp)、[include/minijs/chunk.h](../include/minijs/chunk.h)、[src/chunk.cpp](../src/chunk.cpp)
- VM 执行循环、调用帧、内置函数和 GC 入口：[include/minijs/vm.h](../include/minijs/vm.h)、[src/vm.cpp](../src/vm.cpp)
- 运行时值和 GC 对象：[include/minijs/value.h](../include/minijs/value.h)、[src/value.cpp](../src/value.cpp)、[include/minijs/gc_object.h](../include/minijs/gc_object.h)、[src/gc_object.cpp](../src/gc_object.cpp)

## Value

运行时值由 `minijs::Value` 表示。AST Interpreter 仍主要使用 `Value` 中的共享指针兼容结构；bytecode VM 内部已经把字符串、数组、普通对象、类、实例、闭包和原生函数迁入 `Obj*` GC 堆，并在 `VM::run()` 返回前通过兼容层复制成外部可观察的 `Value`。

当前支持的值类型：

- Number
- Boolean
- Null
- Undefined
- String
- Array
- Object
- Function
- BuiltinFunction
- BytecodeFunction / BytecodeClosure
- BytecodeClass / BytecodeInstance / BytecodeBoundMethod
- GcString / GcArray / GcObject / GcClass / GcInstance / GcClosure

数组、对象和实例具有引用语义：

```javascript
let a = [1];
let b = a;

b[0] = 9;
print(a[0]); // 9
```

## Environment

AST Interpreter 中，`Environment` 负责保存变量名到运行时值的绑定，并且可以指向父环境。

```text
current environment
    -> parent environment
        -> global environment
```

变量查找和赋值会沿着这条父环境链向外查找。

bytecode VM 中，全局变量保存在 `globals_`，局部变量和参数保存在操作数栈与调用帧中，闭包捕获的变量通过 upvalue 访问。

## 函数与闭包

AST Interpreter 的函数值保存两部分信息：

- `FunctionStmt` 函数声明节点
- 函数定义时所在的环境

第二部分就是闭包环境。

```javascript
function outer() {
  let x = 10;

  function inner() {
    return x;
  }

  return inner;
}
```

当 `inner` 被返回后，它仍然保存着包含 `x` 的外层环境。

bytecode VM 中，函数模板由 `ObjFunction` 保存，运行时可调用值由 `ObjClosure` 保存，捕获变量由 `ObjUpvalue` 表示。详见 [Bytecode Closure 设计](bytecode-closure.md)。

## 内置函数

内置函数由 C++ 实现。默认 VM 路径在 `VM::defineBuiltin()` 中注册；AST Interpreter 路径在 `Interpreter::defineBuiltins()` 中注册。

默认 VM 路径当前内置函数：

- `print(value)`
- `clock()`
- `has(value, key)`
- `keys(value)`
- `del(value, key)`
- `typeOf(value)`
- `len(value)`

内置函数会作为全局绑定存在，因此可以像普通值一样赋值和调用：

```javascript
let p = print;
p("hello");
```

## 控制流信号

`return`、`break` 和 `continue` 在 AST Interpreter 内部通过 C++ 控制流信号实现；在 bytecode VM 中则由编译器生成跳转、返回和 upvalue close 相关指令。

AST Interpreter 会在正确的边界捕获这些信号：

- 函数调用捕获 `return`
- 循环捕获 `break` 和 `continue`
- 顶层执行遇到非法的 `return`、`break` 或 `continue` 时报告运行时错误

## 循环

`while` 会在条件为 truthy 时重复执行循环体。

`for` 拥有自己的循环环境，因此 `let` 初始化变量不会泄漏到外层：

```javascript
for (let i = 0; i < 3; i = i + 1) {
  print(i);
}

// i 在这里不可见
```

`for` 循环中的 `continue` 仍然会先执行 increment 表达式，然后再重新判断条件。

## 相等比较

`==` 和 `!=` 使用 `Value::equals` 实现。

当前规则：

- number、boolean、string 按值比较
- `null` 只等于 `null`
- `undefined` 只等于 `undefined`
- array 和 object 按引用身份比较
- function 按函数声明和闭包环境身份比较
- 不同类型的值不相等
