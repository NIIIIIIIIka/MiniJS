# MiniJSVM 项目规划

## 1. 项目定位

本项目的目标不是简单复刻 QuickJS，而是通过亲手实现一个 Tiny JavaScript Engine，系统理解解释器、字节码虚拟机、对象系统和垃圾回收。

QuickJS 的代码量相对克制，但它支持完整度很高的 JavaScript 语义，包括闭包、async/await、module、class、generator、proxy、promise 和异常处理。这些功能背后包含大量复杂机制，不适合作为第一阶段目标。

因此，本项目采用渐进式路线：

```text
Crafting Interpreters(Lox)
    -> Lua 5.1 VM
    -> QuickJS 架构参考
```

目标是在 8 到 12 周内实现一个能完整讲清楚原理、具备技术含量、适合展示在简历中的小型 JavaScript 引擎。

## 2. 最终目标

最终希望支持如下程序：

```javascript
let a = 10;
let b = 20;

function add(x, y) {
    return x + y;
}

print(add(a, b));
```

运行方式：

```bash
$ minijsvm demo.js

30
```

核心能力：

- 变量声明与赋值
- 函数定义与调用
- `if` / `while`
- 常见运算符
- 对象与数组
- 字节码编译
- 栈式虚拟机
- 垃圾回收

预计总代码量：

```text
5000 ~ 10000 行
```

## 3. 总体架构

整体执行链路：

```text
Source Code
    ↓
Lexer
    ↓
Parser
    ↓
AST
    ↓
Bytecode
    ↓
VM
    ↓
Runtime
```

推荐模块划分：

```text
minijsvm/
├── lexer/
├── parser/
├── ast/
├── compiler/
├── vm/
├── runtime/
├── gc/
├── object/
├── tests/
└── main.cpp
```

## 4. 阶段路线

### 阶段 1：Lexer

预计时间：1 周

目标是把源码切分为 Token 流。

示例输入：

```javascript
let a = 10 + 20;
```

期望输出：

```text
LET
IDENT(a)
=
NUMBER(10)
+
NUMBER(20)
;
```

初始 Token 设计：

```cpp
enum class TokenType
{
    Number,
    String,
    Identifier,

    Plus,
    Minus,
    Mul,
    Div,

    Assign,

    Let,

    Semicolon,

    Eof
};
```

核心文件：

```text
lexer.h
lexer.cpp
token.h
```

完成标准：

```cpp
Lexer lexer(source);

while (...)
{
    Token tk = lexer.next();
    std::cout << tk << std::endl;
}
```

能够正确打印 Token 流。

### 阶段 2：Parser

预计时间：1 到 2 周

目标是实现递归下降解析器，先支持表达式解析。

示例：

```javascript
1 + 2 * 3
```

生成 AST：

```text
      +
     / \
    1   *
       / \
      2   3
```

基础 AST 节点：

```text
Expression
    BinaryExpr
    NumberExpr
    VariableExpr
```

示例设计：

```cpp
class Expr
{
};

class BinaryExpr : public Expr
{
};

class NumberExpr : public Expr
{
};
```

完成标准：

- 能解析数字、变量和二元表达式
- 能正确处理运算符优先级
- 能输出或调试查看 AST 结构

### 阶段 3：AST 解释执行

预计时间：1 周

在进入 VM 之前，先直接解释执行 AST。

示例：

```javascript
1 + 2 * 3
```

执行：

```cpp
eval(ast);
```

结果：

```text
7
```

这一阶段重点理解：

```text
Parser
↓
AST
↓
Interpreter
```

AST Interpreter 是验证语法树正确性的最直接方式，也能为后续字节码编译器提供语义参考。

### 阶段 4：变量与作用域

预计时间：1 周

支持：

```javascript
let x = 10;
let y = x + 20;
```

实现 `Environment`：

```cpp
class Environment
{
    Environment* parent;
};
```

内部可先使用：

```cpp
std::unordered_map<std::string, Value>
```

支持块级作用域：

```javascript
{
    let x = 1;
}
```

完成标准：

- 支持 `let`
- 支持变量读取和赋值
- 支持嵌套作用域
- 支持块级作用域

### 阶段 5：函数系统

预计时间：2 周

支持：

```javascript
function add(a, b)
{
    return a + b;
}

add(1, 2);
```

新增运行时对象：

```text
FunctionObject
```

引入调用栈：

```text
CallFrame
```

结构示意：

```text
VM
├── Frame1
├── Frame2
└── Frame3
```

这一阶段是整个项目最重要的部分。它会决定后续闭包、VM、异常处理等能力的扩展方式。

完成标准：

- 支持函数声明
- 支持函数调用
- 支持参数传递
- 支持 `return`
- 支持嵌套调用栈

### 阶段 6：Bytecode VM

预计时间：2 到 3 周

将 AST 编译为字节码，再由 VM 执行。

示例：

```javascript
1 + 2
```

生成：

```text
PUSH 1
PUSH 2
ADD
RET
```

Opcode 设计：

```cpp
enum Opcode
{
    OP_PUSH,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,

    OP_CALL,
    OP_RET
};
```

VM 执行模型：

```cpp
while (true)
{
    switch (op)
    {
        // ...
    }
}
```

完成标准：

- 能将基础表达式编译为字节码
- 能执行栈式字节码
- 支持函数调用指令
- 支持字节码调试输出

到这一阶段，项目已经非常接近 QuickJS 的核心执行模型。

### 阶段 7：对象与数组

预计时间：2 周

支持对象字面量：

```javascript
let p = {
    name: "Tom",
    age: 18
};
```

对象设计：

```cpp
class JSObject
{
    std::unordered_map<std::string, Value> props;
};
```

支持属性访问：

```javascript
p.name
```

数组可作为特殊对象逐步实现：

```javascript
let arr = [1, 2, 3];
print(arr[0]);
```

完成标准：

- 支持对象字面量
- 支持属性读写
- 支持数组字面量
- 支持下标访问

### 阶段 8：垃圾回收

预计时间：2 周

建议先实现引用计数，而不是一开始就做 Mark-Sweep。

基础对象：

```cpp
class GCObject
{
    int refCount;
};
```

对象创建时：

```cpp
retain();
```

对象释放时：

```cpp
release();
```

后续再扩展：

```text
Cycle Collector
```

这与 QuickJS 的整体思路更接近：以引用计数为主，再处理循环引用问题。

完成标准：

- 所有堆对象继承或关联 `GCObject`
- `Value` 能安全持有对象引用
- 变量覆盖、作用域退出、函数返回时能正确释放对象
- 能通过测试证明常见对象不会泄漏

## 5. 版本里程碑

### V0.1：表达式

支持：

```javascript
1 + 2 * 3
```

目标：

- Lexer
- Parser
- AST
- AST Interpreter

### V0.2：变量

支持：

```javascript
let x = 10;
```

目标：

- `let`
- `Environment`
- 变量读取与赋值

### V0.3：控制流

支持：

```javascript
if
while
```

目标：

- 条件判断
- 循环
- 布尔值
- 比较运算符

### V0.4：函数

支持：

```javascript
function
return
```

目标：

- 函数声明
- 函数调用
- 参数传递
- 调用栈

### V0.5：字节码虚拟机

支持：

```javascript
bytecode vm
```

目标：

- Bytecode Compiler
- Stack-based VM
- 字节码调试输出

### V0.6：对象与数组

支持：

```javascript
object
array
```

目标：

- 对象字面量
- 属性访问
- 数组字面量
- 下标访问

### V1.0：运行时完善

支持：

```javascript
gc
module
builtin
```

目标：

- Reference Counting GC
- 基础内建函数
- 简单模块系统
- 更完整的错误报告

## 6. 简历展示建议

项目名称建议避免直接叫 `TinyJS`，可以考虑：

- `MiniJSVM`
- `ByteScript`
- `MiniScriptVM`

简历关键词：

```text
Recursive Descent Parser
Pratt Parser
Bytecode Compiler
Stack-based VM
Closure
Reference Counting GC
Embeddable Runtime
```

项目描述示例：

```text
Implemented a tiny JavaScript-like engine in C++, including lexer, parser,
AST interpreter, bytecode compiler, stack-based virtual machine, object model,
function calls, lexical scope, and reference-counting garbage collection.
```

如果后续继续扩展，可以补充：

- Closure
- Native Function Binding
- Module Loader
- Bytecode Dump Tool
- Debug Trace Mode
- Cycle Collector

## 7. 推荐实现顺序

建议不要一开始追求完整 JavaScript 兼容性，而是按照以下顺序推进：

```text
1. 表达式解释器
2. 变量与作用域
3. 控制流
4. 函数调用
5. 字节码编译
6. 栈式虚拟机
7. 对象系统
8. 引用计数 GC
9. 闭包
10. 简单模块系统
```

这样的路线可以避免过早陷入 JavaScript 复杂语法细节，同时保留解释器和虚拟机最核心的技术含量。

## 8. 参考项目

- QuickJS: <https://github.com/bellard/quickjs>
- Crafting Interpreters: <https://craftinginterpreters.com/>
- Lua 5.1: <https://www.lua.org/source/5.1/>

