# MiniJSVM

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

## 构建命令

配置项目：

```powershell
cmake -S . -B build
```

构建 Debug 版本：

```powershell
cmake --build build --config Debug
```

构建 Release 版本：

```powershell
cmake --build build --config Release
```

## 测试命令

构建并运行测试：

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## 当前进度

- [x] 制定项目路线图
- [x] 整理 QuickJS 架构文档
- [x] 制定详细实施计划
- [x] 创建基础目录结构
- [x] 完成 CMake 配置
- [x] 实现最小命令行程序
- [ ] 实现 Lexer
- [ ] 实现 Parser 与 AST
- [ ] 实现 AST Interpreter
- [ ] 实现变量、作用域与控制流
- [ ] 实现函数系统
- [ ] 实现 Bytecode Compiler
- [ ] 实现 Stack-based VM
- [ ] 实现对象与数组
- [ ] 实现引用计数垃圾回收
