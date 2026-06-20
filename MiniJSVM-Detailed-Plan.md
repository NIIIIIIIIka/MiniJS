# MiniJSVM 详细实施计划

本文档是 [MiniJSVM-Roadmap.md](./MiniJSVM-Roadmap.md) 的执行版。路线图回答“要做什么”，本文档回答“按什么顺序做、每一步产出什么、如何判断完成”。

计划按每周 10 到 15 小时、12 个实施周设计，开始前另安排 2 到 3 天完成“第 0 周”的工程准备。如果每周投入时间更多，可以压缩到 8 到 10 周，但不要跳过测试和阶段验收。

## 1. V1.0 范围

### 1.1 必须支持

```javascript
// 值
undefined;
null;
true;
false;
123;
3.14;
"hello";

// 变量与作用域
let x = 10;
x = x + 1;
{
    let y = x;
}

// 控制流
if (x > 5) {
    print(x);
} else {
    print(0);
}

while (x > 0) {
    x = x - 1;
}

// 函数
function add(a, b) {
    return a + b;
}

// 对象与数组
let user = { name: "Tom", age: 18 };
user.age = 19;

let numbers = [1, 2, 3];
numbers[0] = 10;
```

### 1.2 V1.0 暂不支持

- `var` 和 `const`
- 自动分号插入
- `for`、`switch`、`break`、`continue`
- 箭头函数
- `this`、`new`、prototype 和 class
- 闭包与 upvalue
- `try/catch/finally`
- module、Promise、async/await、generator
- Proxy、Symbol、BigInt、RegExp
- JavaScript 隐式类型转换的全部细节

闭包和简单模块系统放在 V1.1。第一版明确缩小范围，避免语法兼容问题掩盖 VM、对象和 GC 的学习目标。

## 2. 语言语义约定

MiniJSVM 是 JavaScript-like engine，不在 V1.0 宣称 ECMAScript 兼容。

统一约定：

- 语句必须以分号结束，块语句除外
- `let` 具有块级作用域，禁止同一作用域重复声明
- 读取未声明变量是运行时错误
- 赋值给未声明变量是运行时错误
- 条件判断使用简化 truthy 规则
- 算术运算只接受 Number，`+` 额外支持 String 拼接
- 比较操作先只支持同类型值
- 数组越界读取返回 `undefined`
- 函数参数不足时补 `undefined`，多余参数暂时忽略
- 普通函数没有隐式 `this`
- 函数没有显式 `return` 时返回 `undefined`

简化 truthy 规则：

```text
false、null、undefined、0、空字符串 -> false
其他值                               -> true
```

## 3. 技术选型

- 语言：C++17
- 构建：CMake 3.20+
- 测试：CTest + 项目内轻量测试宏，暂不引入第三方测试框架
- 内存检查：AddressSanitizer；Windows/MSVC 不方便时使用对象计数断言
- 错误处理：前端阶段使用带源码位置的错误对象；V1.0 内部可使用 C++ exception 中断当前执行
- 代码风格：类型使用 `PascalCase`，函数与变量使用 `camelCase`

构建目标：

```text
minijs_core     核心静态库
minijs          命令行解释器
minijs_tests    单元测试
```

常用命令：

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 4. 建议目录结构

```text
MiniJS/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── MiniJSVM-Roadmap.md
│   ├── MiniJSVM-Detailed-Plan.md
│   └── QuickJS-Architecture-Guide.md
├── include/minijs/
│   ├── token.h
│   ├── lexer.h
│   ├── ast.h
│   ├── parser.h
│   ├── value.h
│   ├── environment.h
│   ├── interpreter.h
│   ├── opcode.h
│   ├── chunk.h
│   ├── compiler.h
│   ├── object.h
│   ├── vm.h
│   └── runtime.h
├── src/
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── ast_printer.cpp
│   ├── value.cpp
│   ├── environment.cpp
│   ├── interpreter.cpp
│   ├── chunk.cpp
│   ├── compiler.cpp
│   ├── object.cpp
│   ├── vm.cpp
│   ├── runtime.cpp
│   └── main.cpp
├── tests/
│   ├── test_lexer.cpp
│   ├── test_parser.cpp
│   ├── test_interpreter.cpp
│   ├── test_compiler.cpp
│   ├── test_vm.cpp
│   ├── test_object.cpp
│   └── test_gc.cpp
└── examples/
    ├── arithmetic.js
    ├── fibonacci.js
    ├── objects.js
    └── arrays.js
```

在项目真正开始编码时，可以把当前根目录中的三份文档移动到 `docs/`。移动不是本计划本身的必要工作。

## 5. 核心数据结构

### 5.1 SourceLocation 与诊断

每个 Token 至少保存：

```cpp
struct SourceLocation
{
    std::size_t offset;
    std::size_t line;
    std::size_t column;
};

struct Token
{
    TokenType type;
    std::string_view lexeme;
    SourceLocation location;
};
```

错误信息目标：

```text
demo.js:3:12: error: expected ')' after arguments
```

不要只返回 `parse error`，源码位置应从第一周开始贯穿所有阶段。

### 5.2 AST

表达式节点：

```text
LiteralExpr
VariableExpr
AssignExpr
UnaryExpr
BinaryExpr
LogicalExpr
CallExpr
GetExpr
SetExpr
IndexExpr
ArrayExpr
ObjectExpr
```

语句节点：

```text
ExprStmt
LetStmt
BlockStmt
IfStmt
WhileStmt
FunctionStmt
ReturnStmt
```

AST 所有权建议使用：

```cpp
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;
```

### 5.3 Value

第一版用清晰优先的 tagged union：

```cpp
enum class ValueType
{
    Undefined,
    Null,
    Boolean,
    Number,
    Object
};
```

String、Function 和 Array 都作为堆对象，由 Object 指针表示。先保证所有权正确，再考虑 NaN boxing 或整数快路径。

### 5.4 Bytecode Chunk

```cpp
struct Chunk
{
    std::vector<std::uint8_t> code;
    std::vector<Value> constants;
    std::vector<std::uint32_t> lines;
};
```

`lines` 用于把字节码偏移映射回源码行号。第一版可一条指令存一个行号，后续再压缩。

### 5.5 CallFrame

```cpp
struct CallFrame
{
    FunctionObject* function;
    std::size_t ip;
    std::size_t stackBase;
};
```

VM 保持：

```text
value stack
call frame stack
global environment
current runtime
```

## 6. 最小语法

建议用递归下降解析语句，用 Pratt Parser 解析表达式。

```ebnf
program        -> declaration* EOF ;

declaration    -> functionDecl
                | letDecl
                | statement ;

functionDecl   -> "function" IDENTIFIER "(" parameters? ")" block ;
parameters     -> IDENTIFIER ( "," IDENTIFIER )* ;
letDecl        -> "let" IDENTIFIER ( "=" expression )? ";" ;

statement      -> expressionStmt
                | block
                | ifStmt
                | whileStmt
                | returnStmt ;

block          -> "{" declaration* "}" ;
ifStmt         -> "if" "(" expression ")" statement
                  ( "else" statement )? ;
whileStmt      -> "while" "(" expression ")" statement ;
returnStmt     -> "return" expression? ";" ;
expressionStmt -> expression ";" ;

expression     -> assignment ;
assignment     -> ( call "." IDENTIFIER | call "[" expression "]" | IDENTIFIER )
                  "=" assignment
                | logicalOr ;
logicalOr      -> logicalAnd ( "||" logicalAnd )* ;
logicalAnd     -> equality ( "&&" equality )* ;
equality       -> comparison ( ( "==" | "!=" ) comparison )* ;
comparison     -> term ( ( ">" | ">=" | "<" | "<=" ) term )* ;
term           -> factor ( ( "+" | "-" ) factor )* ;
factor         -> unary ( ( "*" | "/" | "%" ) unary )* ;
unary          -> ( "!" | "-" ) unary | call ;
call           -> primary ( "(" arguments? ")"
                          | "." IDENTIFIER
                          | "[" expression "]" )* ;
arguments      -> expression ( "," expression )* ;
primary        -> NUMBER | STRING | "true" | "false"
                | "null" | "undefined" | IDENTIFIER
                | "(" expression ")"
                | arrayLiteral | objectLiteral ;
arrayLiteral   -> "[" ( expression ( "," expression )* )? "]" ;
objectLiteral  -> "{" ( property ( "," property )* )? "}" ;
property       -> ( IDENTIFIER | STRING ) ":" expression ;
```

对象字面量与块语句都以 `{` 开头，但所在语法位置不同：表达式位置解析对象，语句位置解析块。

## 7. 初始 Opcode 设计

按能力逐步增加，不要一开始定义几十条未使用指令。

```text
OP_CONSTANT index
OP_UNDEFINED
OP_NULL
OP_TRUE
OP_FALSE

OP_POP
OP_GET_LOCAL slot
OP_SET_LOCAL slot
OP_GET_GLOBAL name
OP_DEFINE_GLOBAL name
OP_SET_GLOBAL name

OP_NEGATE
OP_NOT
OP_ADD
OP_SUBTRACT
OP_MULTIPLY
OP_DIVIDE
OP_MODULO
OP_EQUAL
OP_GREATER
OP_LESS

OP_JUMP offset
OP_JUMP_IF_FALSE offset
OP_LOOP offset

OP_CALL argc
OP_RETURN

OP_NEW_OBJECT
OP_GET_PROPERTY name
OP_SET_PROPERTY name
OP_NEW_ARRAY count
OP_GET_INDEX
OP_SET_INDEX
```

每条指令必须在同一次提交中补齐：

- 枚举定义
- 编译器生成逻辑
- VM 执行逻辑
- disassembler 输出
- 至少一个测试

## 8. 12 周执行计划

### 第 0 周：工程骨架与约束（2 到 3 天，不计入实施周）

目标：建立可持续迭代的项目骨架。

任务：

- 创建 CMake 工程和三个构建目标
- 建立 `include/`、`src/`、`tests/`、`examples/`
- 实现最小测试 runner
- 定义 `SourceLocation`、`Diagnostic` 和错误输出格式
- CLI 暂时只支持 `minijs <file>` 与 `minijs --help`
- 添加 `.gitignore`，忽略 `build/` 和编译产物
- 在 README 中记录构建与测试命令

产物：

```text
CMakeLists.txt
src/main.cpp
tests/test_main.cpp
README.md
```

验收：

- 全新目录可以成功 configure、build、test
- `minijs --help` 返回 0
- 文件不存在时输出可读错误并返回非 0

### 第 1 周：Lexer

目标：稳定地产生带源码位置的 Token 流。

任务：

- 定义所有 V1.0 TokenType
- 识别单字符和双字符运算符
- 识别 identifier 与 keyword
- 解析十进制整数和小数
- 解析字符串及 `\\n`、`\\t`、`\\"`、`\\\\`
- 跳过空白、换行和 `//` 单行注释
- 对非法字符和未终止字符串报告位置
- 添加 `--dump-tokens`

测试：

- 空输入只产生 EOF
- 运算符无空格连续出现
- keyword 与相似 identifier 区分，例如 `let` 与 `letter`
- 数字 `0`、`12`、`3.14`
- 字符串转义
- 多行输入位置正确
- 非法字符和未终止字符串

验收示例：

```bash
minijs --dump-tokens examples/arithmetic.js
```

输出中每个 token 都包含类型、文本、行和列。

### 第 2 周：表达式 Parser 与 AST

目标：正确解析表达式并可视化 AST。

任务：

- 定义表达式 AST 节点
- 实现 Pratt Parser 或 precedence climbing
- 支持 literal、grouping、unary、binary
- 支持比较与逻辑运算
- 实现 panic mode，遇到错误后至少能停止在明确位置
- 实现 AST Printer
- 添加 `--dump-ast`

测试：

- `1 + 2 * 3` 的树形结构
- `(1 + 2) * 3`
- `!-1`
- `1 < 2 == true`
- 缺失右括号
- 表达式意外结束

验收：

```text
1 + 2 * 3 -> (+ 1 (* 2 3))
```

### 第 3 周：语句 Parser 与 AST Interpreter

目标：完成 V0.1，并能直接解释基础程序。

任务：

- 增加 `ExprStmt` 和 `BlockStmt`
- 完成 `Value` 的 primitive 部分
- 实现 unary、binary、logical 的 AST eval
- 定义运行时错误类型
- 实现 `print` 临时内建函数或 CLI 表达式输出
- 明确除零、类型错误和相等规则

测试：

- 四则运算和优先级
- 布尔逻辑与短路求值
- 字符串拼接
- 非法类型运算
- 除零行为
- 多条表达式语句按顺序执行

退出条件：

- V0.1 示例工作
- Parser 测试与 Interpreter 测试分离
- 所有错误带源码位置

### 第 4 周：变量、作用域与控制流

目标：一次完成 V0.2 和 V0.3。

任务：

- 解析 `let`、赋值、block、if/else、while
- 实现链式 `Environment`
- 块进入时创建子环境，退出时恢复父环境
- 支持变量遮蔽
- 拒绝同一作用域重复声明
- 实现 truthy 和短路逻辑
- 给 while 测试设置执行步数上限，避免测试卡死

测试：

- 声明、读取、赋值
- 未初始化变量得到 `undefined`
- 未声明变量报错
- 同级重复声明报错
- 内层遮蔽不改变外层变量
- if 两个分支
- while 执行 0 次、1 次和多次

验收程序：

```javascript
let n = 5;
let total = 0;
while (n > 0) {
    total = total + n;
    n = n - 1;
}
print(total);
```

输出 `15`。

### 第 5 周：AST 函数系统

目标：在 AST Interpreter 中完成 V0.4，先把函数语义做正确。

任务：

- 解析 function declaration、call 和 return
- 定义 `Callable` / `FunctionObject`
- 函数对象保存参数、函数体和声明环境
- 调用时创建函数环境并绑定参数
- 用内部 `ReturnSignal` 提前退出函数体
- 实现 arity 检查
- 将 `print` 改为正式 `NativeFunction`
- 限制最大递归深度并给出错误

测试：

- 无参数与多参数函数
- 参数不足和参数过多
- 默认返回 `undefined`
- return 提前退出
- 嵌套函数调用
- 递归 factorial 或 fibonacci
- 从全局读取变量

说明：

V1.0 可以让函数保存声明环境，但不承诺闭包生命周期完全正确。闭包的正式实现放在字节码 VM 稳定后的 V1.1。

### 第 6 周：Chunk、Compiler 与反汇编器

目标：把表达式与局部变量编译成可检查的字节码。

任务：

- 定义 `Opcode`、`Chunk`、constant pool
- 实现 bytecode writer 与 operand 编码
- 实现 disassembler
- 编译 literal、unary、binary、expression statement
- 编译全局变量和局部变量
- 编译期维护 scope depth 和 local slot
- 检查单个 chunk 的常量数、局部变量数和字节码长度上限
- 添加 `--dump-bytecode`

测试：

- 编译结果 opcode 序列
- 常量去向正确
- 局部变量 slot 分配正确
- 超出 operand 范围时报编译错误
- disassembler 输出稳定

验收：

```text
1 + 2 * 3;
```

能反汇编为包含 `CONSTANT`、`MULTIPLY`、`ADD`、`POP`、`RETURN` 的合理序列。

### 第 7 周：Stack VM 与控制流

目标：VM 能执行普通表达式、变量、if 和 while。

任务：

- 实现 value stack 和 `ip`
- 实现常量、算术、比较、局部/全局变量指令
- 编译并执行 jump、conditional jump 和 loop
- 实现 jump offset 回填
- VM 运行时错误显示源码行和调用信息
- 添加 `--trace-vm`，逐指令输出栈和 opcode
- 同一组端到端用例同时跑 AST Interpreter 和 VM，比较结果

测试：

- 每个 opcode 的最小测试
- stack underflow 防护
- 错误 jump 防护
- if/else 跳转位置
- while 回跳位置
- AST 与 VM 输出一致

退出条件：

- V0.1 到 V0.3 全部可以经 VM 执行
- 正常用户路径不再依赖 AST Interpreter
- AST Interpreter 仍保留为语义基准

### 第 8 周：函数字节码与 CallFrame

目标：VM 完成 V0.4 和 V0.5。

任务：

- 每个函数编译成独立 `FunctionObject + Chunk`
- 函数对象进入父 chunk 的 constants
- 实现 `OP_CALL` 和 `OP_RETURN`
- 实现 CallFrame stack
- 参数映射到 local slot
- 支持递归和嵌套调用
- 运行时错误打印简化调用栈
- 限制 frame 数量和 value stack 大小

测试：

- 调用 native function
- 调用 bytecode function
- 参数与局部变量槽位不冲突
- 多层嵌套调用
- 递归
- stack overflow
- return 后 stack 恢复正确

验收程序：

```javascript
function fib(n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}
print(fib(10));
```

输出 `55`。

### 第 9 周：对象系统

目标：完成普通对象的创建、属性读写和生命周期管理准备。

任务：

- 定义 `GCObject` 和 `ObjectKind`
- 实现 `StringObject`、`OrdinaryObject`、`FunctionObject`
- 对象属性第一版使用 `unordered_map<string, Value>`
- 解析 object literal、property get/set
- 编译对象相关 opcode
- 属性不存在时返回 `undefined`
- 对非对象访问属性时报告类型错误
- 暂不实现 prototype chain

测试：

- 空对象与多属性对象
- 属性读写
- 动态增加属性
- 属性覆盖
- 缺失属性
- 多个对象属性互不干扰

验收程序：

```javascript
let user = { name: "Tom", age: 18 };
user.age = user.age + 1;
print(user.age);
```

输出 `19`。

### 第 10 周：数组与统一属性访问

目标：完成 V0.6。

任务：

- 实现 `ArrayObject`
- 解析 array literal 和 index expression
- 编译 get/set index
- 数组索引只接受非负整数
- 越界读取返回 `undefined`
- 超出当前长度写入时扩展并填充 `undefined`
- 增加只读 `length` 属性
- 明确对象字符串 key 与数组 index 的分发规则

测试：

- 空数组与多元素数组
- 索引读写
- 越界读写
- 非整数和负数索引错误
- `length`
- 数组内保存对象、函数和数组

验收程序：

```javascript
let values = [1, 2, 3];
values[1] = 20;
print(values[1]);
print(values.length);
```

输出 `20` 和 `3`。

### 第 11 周：引用计数 GC

目标：完成确定性的堆对象生命周期管理。

任务：

- `GCObject` 增加 refCount、retain、release
- `Value` 实现复制、移动、析构规则
- 容器存入和移除 Value 时正确更新引用计数
- Environment、constant pool、value stack、object properties 都遵守所有权约定
- 释放对象时递归释放其持有的 Value
- 增加 runtime live-object counter
- 增加 debug heap dump
- 文档化 borrowed reference 与 owned reference

测试：

- Value copy/move/self-assignment
- 变量覆盖释放旧值
- 作用域退出释放局部对象
- 函数返回对象时对象继续存活
- 数组和对象释放其成员
- native function 参数不被重复释放
- 程序结束后 live-object count 回到预期值

限制：

互相引用的对象仍会泄漏，这是引用计数的已知限制。不要在本周顺带实现 cycle collector。

### 第 12 周：V1.0 收尾

目标：把“能运行”整理成“可展示、可解释、可回归”。

任务：

- 修复所有已知 correctness bug
- 统一错误格式和进程退出码
- 完善 CLI：`--dump-tokens`、`--dump-ast`、`--dump-bytecode`、`--trace-vm`
- 编写 4 个示例程序
- 补充端到端 golden tests
- 用 sanitizer 或对象计数跑完整测试
- README 添加架构图、功能范围、构建方式和示例
- 记录 benchmark，但不把性能作为 V1.0 阻塞条件
- 打 `v1.0.0` tag 前完成干净构建验证

V1.0 最终验收：

- 从空 build 目录可以构建并通过全部测试
- 示例程序输出稳定
- Lexer、Parser、Compiler、VM、Object、GC 都有独立测试
- 所有用户错误都返回非 0，且至少包含行号
- 正常退出后无普通对象泄漏
- README 能让新用户在 5 分钟内运行第一个程序

## 9. 每阶段工作节奏

每个功能都按同一个小循环完成：

```text
1. 写一个失败测试
2. 定义最小接口或 AST/opcode
3. 实现功能
4. 跑模块测试
5. 跑全部回归测试
6. 添加一个端到端示例
7. 更新当前里程碑状态
```

建议单次提交只完成一个可描述的行为：

```text
feat(lexer): tokenize string escapes
feat(parser): parse binary precedence
feat(vm): execute conditional jumps
fix(gc): retain values stored in object properties
test(function): cover recursive calls
```

## 10. 测试策略

### 10.1 单元测试

模块边界分别测试：

- Lexer：源码到 Token
- Parser：Token 到 AST
- Interpreter：AST 到结果
- Compiler：AST 到 Opcode
- VM：Chunk 到结果
- Object：属性和索引语义
- GC：对象生命周期和引用计数

### 10.2 差分测试

在 AST Interpreter 和 VM 同时存在时，同一个源码应得到相同：

- stdout
- 返回值
- 错误类型

这能较早发现字节码编译或 VM 栈管理错误。

### 10.3 端到端测试

至少维护这些脚本：

```text
arithmetic.js
scope.js
control_flow.js
functions.js
fibonacci.js
objects.js
arrays.js
runtime_errors.js
```

脚本输出使用 golden file 比较。

### 10.4 模糊测试

V1.0 收尾时可为 Lexer/Parser 添加轻量 fuzz：

- 随机字节输入不能崩溃
- 任意输入必须成功返回 AST 或诊断
- 不允许无限循环和越界访问

这是加分项，不阻塞 V1.0。

## 11. 错误分类

保持三类错误分离：

```text
LexError      非法字符、未终止字符串
ParseError    缺少 token、语法结构错误
RuntimeError  类型错误、未声明变量、调用非函数
```

退出码建议：

```text
0   成功
64  命令行参数错误
65  词法、语法或编译错误
70  运行时错误
74  文件读取错误
```

## 12. 性能与优化边界

V1.0 只做必要的结构性优化：

- 常量池
- local slot，避免局部变量全部查 map
- 编译期计算跳转偏移
- operand stack 和 frame stack 使用连续存储

V1.0 不做：

- NaN boxing
- inline cache
- hidden class / shape
- string interning / atom
- peephole optimization
- tail-call optimization
- JIT

优化前先建立 benchmark，至少包含：

```text
递归 fib
while 累加
函数调用循环
对象属性读写循环
数组读写循环
```

## 13. 主要风险与应对

### 范围膨胀

风险：不断加入 JavaScript 新语法，主线迟迟无法闭环。

应对：任何新功能先放入 V1.1 backlog，只有修复现有语义所必需的内容才能进入 V1.0。

### AST 与 VM 语义不一致

风险：两套执行器对 truthy、相等、错误行为有不同实现。

应对：把 `Value` 运算与 truthy 规则放到 runtime 公共函数，并持续执行差分测试。

### 引用计数错误

风险：复制时漏 retain、销毁时重复 release，导致泄漏或 use-after-free。

应对：优先使用 RAII 封装 `Value`，禁止业务代码直接修改 refCount，并对 live-object count 做测试断言。

### CallFrame 与栈槽混乱

风险：函数返回后留下参数或临时值，递归时出现难定位错误。

应对：每个 frame 保存明确的 `stackBase`，为每次 call/return 编写栈布局测试，并用 `--trace-vm` 观察执行。

### 错误位置丢失

风险：到了 VM 阶段只能看到“运行失败”，无法回到源码。

应对：Token、AST 和 Chunk 都保存位置或行号映射，这项基础从第 0 周开始建立。

## 14. V1.1 Backlog

V1.0 完成后，推荐按下面顺序扩展：

1. Closure 与 Upvalue
2. AtomTable 与字符串驻留
3. Prototype chain 与方法调用
4. Cycle Collector
5. 字节码文件序列化
6. 简单模块加载器
7. `for`、`break`、`continue`
8. `try/catch/finally`

闭包是最值得优先做的 V1.1 功能，因为它会逼你真正理解局部变量、栈帧生命周期和堆上 upvalue。

## 15. 里程碑检查表

- [ ] V0.1：表达式可经 AST Interpreter 执行
- [ ] V0.2：变量与块级作用域完成
- [ ] V0.3：if/while 完成
- [ ] V0.4：函数与 return 完成
- [ ] V0.5：Bytecode Compiler 与 Stack VM 完成
- [ ] V0.6：对象与数组完成
- [ ] V1.0：引用计数、CLI、文档和完整测试完成

每个版本只有在测试、示例和错误路径都完成后才能勾选。代码“看起来能跑”不算通过里程碑。
