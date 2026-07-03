# MiniJS 运行时设计

本文档记录 MiniJS 当前 AST Interpreter 阶段的运行时设计。

## 执行流程

MiniJS 当前通过以下流程执行源代码：

```text
Source Code
    -> Lexer
    -> Parser
    -> AST
    -> Interpreter
    -> Runtime Value
```

当前解释器直接遍历 AST 执行程序。Bytecode Compiler 和 Stack-based VM 会在后续阶段实现。

## Value

运行时值由 `minijs::Value` 表示。

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

数组和对象通过共享指针保存，因此具有引用语义：

```javascript
let a = [1];
let b = a;

b[0] = 9;
print(a[0]); // 9
```

## Environment

`Environment` 负责保存变量名到运行时值的绑定，并且可以指向父环境。

```text
current environment
    -> parent environment
        -> global environment
```

变量查找和赋值会沿着这条父环境链向外查找。

## 函数与闭包

MiniJS 的函数值保存两部分信息：

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

## 内置函数

内置函数由 C++ 实现，并在 `Interpreter::defineBuiltins()` 中注册。

当前内置函数：

- `print(value)`
- `has(value, key)`
- `keys(value)`
- `del(value, key)`
- `typeOf(value)`
- `len(value)`

内置函数会作为 `ValueType::BuiltinFunction` 存在于全局环境中，因此可以像普通值一样赋值和调用：

```javascript
let p = print;
p("hello");
```

## 控制流信号

`return`、`break` 和 `continue` 在解释器内部通过 C++ 控制流信号实现。

解释器会在正确的边界捕获这些信号：

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
