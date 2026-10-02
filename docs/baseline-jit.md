# Baseline JIT 设计

本文档基于当前 MiniJS bytecode VM 进度，整理 Baseline JIT 的近期实现方案。当前代码已经具备 JIT 热度计数、调度状态、baseline 编译入口、函数入口 dispatch、稳定 Runtime ABI、第一版 ARM64 stub compiler，以及一个可复用 VM runtime helper 的 decoded bytecode baseline executor。

## 当前状态

- [x] `BytecodeFunction` 持有 `JitFeedback`，记录 `callCount`、`backedgeCount` 和 `JitState`。
- [x] `VM` 持有 `JitOptions`，可以通过 `setJitEnabled()`、`setJitCallThreshold()`、`setJitBackedgeThreshold()` 控制热度收集。
- [x] 函数调用通过 `recordFunctionCall()` 计数，循环回边通过 `recordLoopBackedge()` 计数。
- [x] 达到阈值后，`maybeScheduleJit()` 会把函数状态从 `Cold` 切到瞬时 `Scheduled`，随后立刻尝试 baseline 编译并进入 `Compiled` 或 `Failed`。
- [x] `bytecode_decoder` 已能解析指令边界、操作数、跳转目标和 feedback slot 类型，可作为 Baseline JIT 前端。
- [x] `FeedbackSlot` 已承载属性读写和方法调用 inline cache，可为后续代码生成提供 shape/class 反馈。
- [x] `compileBaseline()` 已接入 `verifyBytecode()`，并缓存第四阶段支持 opcode 的 `DecodedInstruction`。
- [x] `JitFeedback` 已保存 baseline code handle 和编译失败原因。
- [x] `VM` 已有测试用 baseline executor 入口，可直接执行纯算术、local、return 和简单 loop。
- [x] VM 调用 compiled code 的入口已接入 `callBytecodeClosure()`。
- [x] Baseline executor 已复用 VM runtime helper 支持 global、array/index、object/property、upvalue 和 current-closure opcode。
- [x] `BaselineFrame` 和 `BaselineEntry` 已定义，第一批 `extern "C"` runtime helper 已通过稳定入口帧访问 VM。
- [x] `BaselineCompiler` 已能为 `Constant`、`GetLocal`、`Add`、`Return` 生成 ARM64 stub，并写入 executable memory。
- [x] VM baseline dispatch 已能在 ARM64 且存在 native `BaselineEntry` 时优先进入机器码；没有 native entry 时继续使用 decoded executor。

还没有完成：

- [ ] `Call`、`MethodCall`、`SuperCall` 的嵌套调用 frame 协议。
- [ ] `Closure`、`Class`、`Method`、`StaticMethod`、`Inherit` 的 baseline 执行路径。
- [ ] safepoint、deopt 或 fallback 协议。
- [ ] 平台相关 codegen 后端。

## 代码位置

- JIT 状态和热度数据：[include/minijs/bytecode_function.h](../include/minijs/bytecode_function.h) 中的 `JitState`、`JitFeedback`、`BytecodeFunction::jit`
- JIT 选项和调试入口：[include/minijs/vm.h](../include/minijs/vm.h) 中的 `JitOptions`、`setJitEnabled()`、`debugGlobalFunctionJitFeedback()`
- 热度计数和调度：[src/vm.cpp](../src/vm.cpp) 中的 `recordFunctionCall()`、`recordLoopBackedge()`、`maybeScheduleJit()`
- 字节码解码和校验：[include/minijs/bytecode_decoder.h](../include/minijs/bytecode_decoder.h)、[src/bytecode_decoder.cpp](../src/bytecode_decoder.cpp)
- Baseline code object：[include/minijs/baseline_code.h](../include/minijs/baseline_code.h)
- Runtime ABI：[include/minijs/baseline_frame.h](../include/minijs/baseline_frame.h)、[include/minijs/baseline_runtime.h](../include/minijs/baseline_runtime.h)、[src/baseline_runtime.cpp](../src/baseline_runtime.cpp)
- Baseline native compiler：[include/minijs/baseline_compiler.h](../include/minijs/baseline_compiler.h)、[src/baseline_compiler.cpp](../src/baseline_compiler.cpp)
- Executable memory：[include/minijs/executable_memory.h](../include/minijs/executable_memory.h)、[src/executable_memory.cpp](../src/executable_memory.cpp)
- Baseline 编译入口：[include/minijs/baseline_jit.h](../include/minijs/baseline_jit.h)、[src/baseline_jit.cpp](../src/baseline_jit.cpp)
- inline cache 反馈：[include/minijs/chunk.h](../include/minijs/chunk.h)、[docs/inline-cache.md](inline-cache.md)
- 当前测试：[tests/test_bytecode.cpp](../tests/test_bytecode.cpp) 中的 JIT、baseline executor 和 runtime helper dispatch 测试

## 核心原则

Baseline JIT 复用现有 bytecode，不维护第二套 opcode 或第二套栈式 IR。

```text
Stable Bytecode
  -> Interpreter VM
  -> Baseline JIT: DecodedInstruction / Opcode -> executor or native code
  -> Optimizing JIT: Bytecode + feedback -> SSA IR -> optimized code
```

Baseline code object 的职责是缓存已经验证过的 bytecode 视图，减少重复 decode，并为后续 dispatch/native codegen 提供稳定输入。真正需要 Guard、类型特化、Deopt 时，再进入 Optimizing JIT 的 SSA IR。

## BaselineCode 表示

`BaselineCode` 挂在 `JitFeedback` 上，而不是塞进 `Chunk`。`Chunk` 仍然是字节码、常量池和 feedback slot 的所有者。

```cpp
struct BaselineCode {
  std::vector<DecodedInstruction> instructions;
  std::vector<std::size_t> bytecodeOffsetToInstructionIndex;

  std::vector<std::size_t> bytecodeOffsetToNativeOffset;
  std::shared_ptr<ExecutableMemory> executableMemory;
  BaselineEntry entry = nullptr;
};
```

这里没有 `BaselineOp`。executor 和未来 native codegen 都直接读取：

```cpp
instruction.opcode
instruction.operands
instruction.jumpTarget
```

这样 bytecode 仍是唯一语义来源，Baseline 只是更靠近执行端的一层缓存和入口。

## Runtime ABI

第一版 native backend 不直接读取 `std::vector<Value>` 的内部地址，也不依赖 C++ 容器布局。机器码入口只接收稳定帧：

```cpp
struct BaselineFrame {
  VM* vm = nullptr;
  ObjClosure* closure = nullptr;

  std::size_t returnSlot = 0;
  std::size_t slotStart = 0;

  bool returnsReceiver = false;
  bool completed = false;
  bool failed = false;
};

using BaselineEntry = void (*)(BaselineFrame*);
```

ARM64 下 `BaselineFrame*` 会作为第一个参数通过 `x0` 传入。机器码需要读写局部变量、压栈、算术或返回时，必须调用 Runtime ABI helper，例如：

```cpp
extern "C" bool minijsBaselineGetLocal(BaselineFrame* frame, std::uint32_t slot);
extern "C" bool minijsBaselinePushConstant(BaselineFrame* frame,
                                            std::uint32_t constantIndex);
extern "C" bool minijsBaselineAdd(BaselineFrame* frame);
extern "C" bool minijsBaselineReturn(BaselineFrame* frame);
```

这些 helper 直接操作 VM 栈，但只暴露布尔状态给机器码。C++ 异常不会穿过 ABI 边界；helper 捕获异常后设置 `frame->failed = true` 并返回 `false`。`Value` 不作为 C ABI 返回值传递，需要通过 VM 栈或指针参数传入 helper。

## 触发流程

```text
callBytecodeClosure()
  -> recordFunctionCall()
  -> maybeScheduleJit()
  -> compileScheduledJit()
  -> compileBaseline()
  -> verifyBytecode()
  -> cache DecodedInstruction
  -> Compiled 或 Failed
  -> 后续调用优先进入 native BaselineEntry
  -> 没有 native entry 时进入 decoded baseline executor

Opcode::Loop
  -> recordLoopBackedge()
  -> maybeScheduleJit()
```

`Scheduled` 是瞬时状态。支持的函数会进入 `Compiled` 并挂上 `BaselineCode`；不支持的 opcode 或 verifier 失败会进入 `Failed`，之后继续走 VM。达到阈值的那次调用只负责安装 code object，下一次调用才进入 baseline executor。JIT disabled 时即使已有 `BaselineCode` 也继续走 VM。

`compileBaseline()` 会先尝试第一版 native `BaselineCompiler`。如果函数只包含 `Constant`、`GetLocal`、`Add`、`Return`，会生成 ARM64 stub、`bytecodeOffsetToNativeOffset` 映射和 `BaselineEntry`。如果 native compiler 遇到暂不支持的 opcode，会回退到现有 decoded baseline compiler；这样已经支持的 runtime helper opcode 仍能通过 decoded executor 验证语义。

## 函数职责

`BaselineCompiler::compile()` 是 native baseline 的唯一公开入口。它不抛出“unsupported opcode”异常，而是通过 `BaselineCompileResult::error` 返回失败原因；外层 `compileBaseline()` 可以据此回退到 decoded baseline executor。

`Arm64Emitter` 目前只是 `BaselineCompiler` 私有的极小指令写入器。它负责 prologue/epilogue、helper 调用、branch placeholder 和 branch patch，不承担通用 assembler 的职责。

`emitRuntimeCall()` 固定使用 Runtime ABI：把 `BaselineFrame*` 放回 `x0`，把 helper 地址装入 `x16`，通过 `blr x16` 调用。helper 的 `bool` 返回值在 `w0`，机器码随后用 `cbz w0, epilogue` 处理失败路径。

`ExecutableMemory::allocate()` 负责 W^X 生命周期：先申请可写内存并复制机器码，刷新 instruction cache，然后切换为只读可执行内存。释放逻辑集中在 `ExecutableMemory::release()`，供析构和 move assignment 共用。

## 当前 Opcode 到 ARM64 stub 的映射

第一版 native baseline compiler 没有把 `Value` 运算完全内联到机器码里。它生成的是一层很薄的 ARM64 stub：

```text
Opcode
  -> ARM64 stub
  -> Runtime ABI helper
  -> helper 操作 VM stack_
```

也就是说，当前映射重点不是“`OP_ADD` 直接变成若干条加法机器指令”，而是“`OP_ADD` 生成一段调用 `minijsBaselineAdd()` 的机器码”。这样可以先验证 native entry、可执行内存、调用约定和错误返回路径，同时避免机器码直接依赖 `Value`、`std::vector<Value>` 或 GC 对象布局。

每个 native baseline 函数统一包含：

```text
prologue:
  stp x29, x30, [sp, #-16]!
  mov x29, sp
  stp x19, x20, [sp, #-16]!
  mov x19, x0

body:
  按 Opcode 顺序调用 runtime helper

epilogue:
  ldp x19, x20, [sp], #16
  ldp x29, x30, [sp], #16
  ret
```

`x0` 是进入 native baseline 时传入的 `BaselineFrame*`。由于 helper 调用会使用 `x0` 和 `x1` 传参，prologue 会先把 frame 固定保存到 callee-saved 寄存器 `x19`。之后每次调用 helper 前，再把 `x19` 复制回 `x0`。

当前支持的 opcode 映射如下：

| Opcode | 操作数 | Runtime helper | 结果 |
| --- | --- | --- | --- |
| `OP_CONSTANT` | `constantIndex` | `minijsBaselinePushConstant(frame, constantIndex)` | 从常量池取值并压入 VM 栈 |
| `OP_GET_LOCAL` | `slot` | `minijsBaselineGetLocal(frame, slot)` | 读取 `slotStart + slot` 的局部槽并压栈 |
| `OP_ADD` | 无 | `minijsBaselineAdd(frame)` | 弹出两个值，执行数字加法或字符串拼接，再压回结果 |
| `OP_RETURN` | 无 | `minijsBaselineReturn(frame)` | 弹出返回值，关闭 upvalue，把结果写回 `returnSlot`，标记 `completed` |

对应的 ARM64 stub 形态如下。

`OP_CONSTANT constantIndex`：

```text
mov x0, x19
mov w1, constantIndex
mov x16, helper
blr x16
cbz w0, epilogue
```

`OP_GET_LOCAL slot`：

```text
mov x0, x19
mov w1, slot
mov x16, helper
blr x16
cbz w0, epilogue
```

`OP_ADD`：

```text
mov x0, x19
mov x16, helper
blr x16
cbz w0, epilogue
```

`OP_RETURN`：

```text
mov x0, x19
mov x16, helper
blr x16
cbz w0, epilogue
b epilogue
```

其中 `mov x16, helper` 不是单条源码级函数，而是由 `emitLoadX16Imm64()` 生成的一组 `movz`/`movk`，把 64 位 helper 地址装入 `x16`：

```text
movz x16, low16
movk x16, next16, lsl #16
movk x16, next16, lsl #32
movk x16, next16, lsl #48
blr  x16
```

如果 helper 返回 `false`，机器码通过 `cbz w0, epilogue` 直接跳到统一出口。回到 C++ 后，`VM::executeBaselineEntry()` 会检查：

```cpp
frame.failed
frame.completed
stack_.empty()
```

并把失败转换成 `RuntimeError`。因此 C++ 异常不会跨过 native ABI 边界。

`OP_RETURN` 额外发射一条无条件跳转到 epilogue，是因为返回成功后函数已经完成，不应该继续执行后续机器码。`minijsBaselineReturn()` 会根据 `BaselineFrame::returnsReceiver` 保持构造器 `init` 的 receiver 返回语义。

`BaselineCode::bytecodeOffsetToNativeOffset` 会记录每条 bytecode 指令对应的机器码起点。当前它主要用于测试和调试，后续 safepoint、deopt 或源码级 profiling 也可以从这里建立 bytecode offset 和 native offset 的关系。

## 当前支持范围

第四阶段先支持低风险 opcode：

- 纯表达式和局部变量：`Constant`、`Add`、`Sub`、`Mul`、`Div`、`Mod`、`Negate`、`Equal`、`Greater`、`Less`、`Not`、`GetLocal`、`SetLocal`、`Pop`、`Return`
- 控制流：`JumpIfFalse`、`Jump`、`Loop`

第六阶段继续支持不引入新调用帧的 runtime helper opcode：

- 全局变量：`DefineGlobal`、`GetGlobal`、`SetGlobal`
- 数组与下标：`Array`、`GetIndex`、`SetIndex`
- 对象与属性 IC：`Object`、`GetProperty`、`SetProperty`
- 闭包访问：`GetUpvalue`、`SetUpvalue`、`CloseUpvalue`、`GetCurrentClosure`

函数调用、方法调用、类定义、闭包创建、继承和 `super` 暂时编译失败并回退 VM。后续扩展时优先直接为现有 `Opcode` 增加执行路径或 runtime helper 路径，不新增平行 opcode。

第七阶段 native compiler 的第一版 ARM64 switch 只覆盖：

- `Constant`
- `GetLocal`
- `Add`
- `Return`

这些指令不会内联访问 `VM::stack_` 或 `std::vector<Value>` 内部地址，而是统一通过 Runtime ABI helper 完成语义。每条 helper 调用后的 `false` 返回值都会被修补成跳转到 epilogue，`Return` 成功后也跳到 epilogue。

## 执行模型

Baseline frame 复用 VM 的调用约定：

- 参数仍位于 VM `stack_` 的 callee/receiver 后方。
- `returnSlot` 和 `slotStart` 与 `CallFrame` 保持一致。
- 局部变量仍使用 bytecode slot 编号。
- 返回值仍写回调用方期待的位置。
- GC roots 仍来自 `stack_`、`frames_`、`openUpvalues_` 和 `temporaryRoots_`。

当前 baseline dispatch 已接入 `callBytecodeClosure()` 的函数入口。ARM64 且 `BaselineCode::entry != nullptr` 时，VM 会优先进入 native `BaselineEntry`；否则继续进入 decoded baseline executor。测试入口仍保留，用于直接验证 code object 和 runtime helper ABI。

非调用型 runtime opcode 通过 VM helper 与解释器共享语义，例如 global 读写、数组创建、对象属性读写和 upvalue 读写。baseline 只负责从 `DecodedInstruction::operands` 取操作数，不直接读取或推进 `frame.ip`。

## 下一步

1. 设计 `Call`、`MethodCall`、`SuperCall` 的嵌套调用 frame 协议。
2. 为 `Closure` 和 class-family opcode 增加 GC 安全的 baseline helper。
3. 扩展 benchmark，用于对比解释执行、IC 与 baseline dispatch 路径。
4. 设计 safepoint、deopt 和 fallback 边界。
5. 逐步把更多 opcode 从 decoded executor 下沉到 native stub 或更细的 runtime helper。
