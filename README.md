# MiniJSVM

MiniJSVM 是一个使用 C++ 编写的小型 JavaScript-like 解释器项目。

当前阶段已经推进到 Bytecode VM 优化与 Baseline JIT 设计准备。长期路线如下：

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
- `if`、`while`、`for`、`break` 和 `continue`
- 算术、比较与逻辑运算符
- 对象与数组
- 类、实例方法、静态方法、继承与 `super`
- 字节码编译与执行
- mark-sweep 垃圾回收

## 当前进度

- [x] 制定项目路线图
- [x] 整理 QuickJS 架构文档
- [x] 制定详细实施计划
- [x] 创建基础目录结构
- [x] 完成 CMake 配置
- [x] 实现最小命令行程序
- [x] 实现 Lexer
- [x] 实现 Parser 与 AST
- [x] 实现 AST Interpreter
- [x] 实现变量、作用域与控制流
- [x] 实现函数、递归与闭包
- [x] 实现 Bytecode Compiler
- [x] 实现 Stack-based VM
- [x] 实现对象、数组、类、实例方法与静态方法
- [x] 实现 Runtime / GC
- [x] 实现属性访问与方法调用的多态内联缓存
- [x] 实现 FeedbackSlot，并将 GetProperty、SetProperty、MethodCall 绑定到 feedback slot
- [x] 实现 cache 失效、IC 统计与 benchmark 对比工具
- [x] 实现 JIT 热度计数与调度状态
- [x] 设计 FeedbackVector / Baseline JIT 代码结构
- [x] 实现 Baseline JIT 编译入口与第四阶段 decoded bytecode executor
- [x] 接入 Baseline JIT 函数入口 dispatch
- [x] 扩展 Baseline JIT runtime helper opcode 路径
- [x] 定义 Baseline Runtime ABI 与稳定入口帧
- [x] 实现第一版 BaselineCompiler，生成 ARM64 stub 和 executable memory
- [ ] 扩展 benchmark，用于对比解释执行、IC 与后续 JIT 路径

## 当前功能

- 递归下降 Parser
- AST Interpreter
- 块级作用域变量
- 数字、布尔值、`null`、`undefined`、字符串
- 数组与对象
- 对象属性读取与赋值
- 函数、递归与闭包
- 类、实例方法、静态方法、继承与 `super`
- 内置函数
- 控制流：`if`、`while`、`for`、`break`、`continue`
- 运算符：算术、比较、相等、逻辑运算

当前内置函数：

- `print(value)`
- `clock()`
- `has(value, "key")`
- `keys(value)`
- `del(value, "key")`
- `typeOf(value)`
- `len(value)`

## 代码导览

- CLI 入口与运行模式：[src/main.cpp](src/main.cpp)
- Lexer / Token：[include/minijs/lexer.h](include/minijs/lexer.h)、[src/lexer.cpp](src/lexer.cpp)、[include/minijs/token.h](include/minijs/token.h)
- Parser / AST：[include/minijs/parser.h](include/minijs/parser.h)、[src/parser.cpp](src/parser.cpp)、[include/minijs/ast.h](include/minijs/ast.h)、[src/ast.cpp](src/ast.cpp)
- AST Interpreter：[include/minijs/interpreter.h](include/minijs/interpreter.h)、[src/interpreter.cpp](src/interpreter.cpp)、[include/minijs/environment.h](include/minijs/environment.h)
- Bytecode 编译与反汇编：[include/minijs/compiler.h](include/minijs/compiler.h)、[src/compiler.cpp](src/compiler.cpp)、[include/minijs/chunk.h](include/minijs/chunk.h)、[src/chunk.cpp](src/chunk.cpp)、[src/disassembler.cpp](src/disassembler.cpp)
- VM / Runtime / GC：[include/minijs/vm.h](include/minijs/vm.h)、[src/vm.cpp](src/vm.cpp)、[include/minijs/value.h](include/minijs/value.h)、[include/minijs/gc_object.h](include/minijs/gc_object.h)、[src/gc_object.cpp](src/gc_object.cpp)
- JIT 准备：[include/minijs/bytecode_function.h](include/minijs/bytecode_function.h)、[include/minijs/bytecode_decoder.h](include/minijs/bytecode_decoder.h)、[include/minijs/baseline_code.h](include/minijs/baseline_code.h)、[include/minijs/baseline_compiler.h](include/minijs/baseline_compiler.h)、[include/minijs/baseline_jit.h](include/minijs/baseline_jit.h)、[include/minijs/executable_memory.h](include/minijs/executable_memory.h)、[src/bytecode_decoder.cpp](src/bytecode_decoder.cpp)、[src/baseline_compiler.cpp](src/baseline_compiler.cpp)、[src/baseline_jit.cpp](src/baseline_jit.cpp)、[src/executable_memory.cpp](src/executable_memory.cpp)、[docs/baseline-jit.md](docs/baseline-jit.md)、[docs/optimizing-jit.md](docs/optimizing-jit.md)
- 示例与测试：[examples/](examples/)、[tests/test_interpreter.cpp](tests/test_interpreter.cpp)、[tests/test_bytecode.cpp](tests/test_bytecode.cpp)

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
- [GC 第一阶段实现说明](docs/gc.md)
- [Hidden Class 与对象存储](docs/hidden-class.md)
- [FeedbackSlot 与 Inline Cache](docs/inline-cache.md)
- [Baseline JIT 设计](docs/baseline-jit.md)
- [Optimize JIT 设计](docs/optimizing-jit.md)
- [表达式 AST 与 Opcode 对照](docs/ast-bytecode-expressions.md)
- [Bytecode Closure 设计](docs/bytecode-closure.md)
- [项目路线图](docs/project-roadmap.md)
- [详细实施计划](docs/implementation-plan.md)
- [QuickJS 架构导读](docs/quickjs-architecture-guide.md)
