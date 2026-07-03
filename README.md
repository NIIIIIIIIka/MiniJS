# MiniJSVM

MiniJSVM 是一个使用 C++ 编写的小型 JavaScript-like 解释器项目。

当前阶段目标是完成一个稳定的 AST Interpreter。长期路线如下：

```text
Source Code
    -> Lexer
    -> Parser
    -> AST Interpreter
    -> Bytecode Compiler
    -> Stack-based VM
    -> Runtime / GC
```

## 项目目标



使用 C++ 从零实现一个小型 JavaScript 引擎，通过完整实现 Lexer、Parser、AST Interpreter、Bytecode Compiler、Stack-based VM、对象系统和垃圾回收，理解解释器与虚拟机的核心原理。

项目计划支持以下 JavaScript 子集：

- 变量与块级作用域
- 函数定义与调用
- `if` 和 `while`
- 算术、比较与逻辑运算符
- 对象与数组
- 字节码编译与执行
- 引用计数垃圾回收

## 当前进度

- [x] 制定项目路线图

- [x]  整理 QuickJS 架构文档
- [x]  制定详细实施计划
- [x]  创建基础目录结构
- [x]  完成 CMake 配置
- [x]  实现最小命令行程序
- [x]  实现 Lexer
- [x]  实现 Parser 与 AST
- [x]  实现 AST Interpreter
- [x]  实现变量、作用域与控制流
- [x]  实现函数系统
- [ ]  实现 Bytecode Compiler
- [ ]  实现 Stack-based VM
- [ ]  实现对象与数组
- [ ]  实现引用计数垃圾回收

## 当前功能

- 递归下降 Parser
- AST Interpreter
- 块级作用域变量
- 数字、布尔值、`null`、`undefined`、字符串
- 数组与对象
- 对象属性读取与赋值
- 函数、递归与闭包
- 内置函数
- 控制流：`if`、`while`、`for`、`break`、`continue`
- 运算符：算术、比较、相等、逻辑运算

当前内置函数：

- `print(value)`
- `has(value, "key")`
- `keys(value)`
- `del(value, "key")`
- `typeOf(value)`
- `len(value)`

## 构建

使用 Ninja：

```powershell
cmake -S . -B build-ninja -G Ninja
cmake --build build-ninja
```

使用默认 CMake 生成器：

```powershell
cmake -S . -B build
cmake --build build
```

## 运行

```powershell
.\build-ninja\minijs.exe examples\basics.js
```

可运行示例：

```powershell
.\build-ninja\minijs.exe examples\basics.js
.\build-ninja\minijs.exe examples\control_flow.js
.\build-ninja\minijs.exe examples\closure_counter.js
.\build-ninja\minijs.exe examples\objects_arrays.js
```

## 测试

```powershell
cmake --build build-ninja
ctest --test-dir build-ninja --output-on-failure
```

如果 MinGW 运行时 DLL 不在 `PATH` 中，可以先添加 Qt MinGW 路径：

```powershell
$env:PATH='.....\mingw1310_64\bin;' + $env:PATH
ctest --test-dir build-ninja --output-on-failure
```

## 文档

- [语言说明](docs/language.md)
- [运行时设计](docs/runtime.md)
- [项目路线图](MiniJSVM-Roadmap.md)
- [详细计划](MiniJSVM-Detailed-Plan.md)
- [QuickJS 架构导读](QuickJS-Architecture-Guide.md)
