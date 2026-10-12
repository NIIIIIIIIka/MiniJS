# Baseline JIT 设计

本文档基于当前 MiniJS bytecode VM 进度，整理 Baseline JIT 的近期实现方案。当前代码已经具备 JIT 热度计数、调度状态、baseline 编译入口、函数入口 dispatch、稳定 Runtime ABI、第一版 ARM64 / Windows x64 stub compiler，以及一个可复用 VM runtime helper 的 decoded bytecode baseline executor。

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
- [x] Baseline executor 已复用 VM runtime helper 支持 global、array/index、object/property、closure/upvalue、class/method/inherit、current-closure、call、method-call 和 super-call opcode。
- [x] `BaselineFrame` 和 `BaselineEntry` 已定义，第一批 `extern "C"` runtime helper 已通过稳定入口帧访问 VM。
- [x] `BaselineCompiler` 已能为局部变量、栈清理、算术/比较/逻辑 opcode、基础控制流和 `Return` 生成 ARM64 和 Windows x64 stub，并写入 executable memory；Windows x64 的 `Negate`、`Add`、`Sub`、`Mul`、`Div` 和 `Mod` 已有 number fast path / guarded number helper。
- [x] VM baseline dispatch 已能在支持 native backend 且存在 native `BaselineEntry` 时优先进入机器码；没有 native entry 时继续使用 decoded executor。

后续扩展与边界状态：

- [x] `Call` 已支持在 baseline 中同步进入被调 bytecode closure 或 native function。
- [x] `MethodCall` 已支持在 baseline 中复用 method inline cache，并同步进入目标方法 closure。
- [x] `SuperCall` 已支持在 baseline 中重排 superclass/receiver 栈槽，并同步进入父类方法 closure。
- [x] `Closure` 已支持在 baseline 中创建运行时 closure，并按 bytecode 携带的元数据捕获 upvalue。
- [x] `Class`、`Method`、`StaticMethod`、`Inherit` 已支持在 baseline 中创建类对象、挂载方法和建立继承关系。
- [x] 编译回退、指令级 helper fallback 和错误退出边界已明确，见下文。
- [ ] 可恢复执行的 deopt 协议和独立 safepoint 元数据。
- [x] ARM64 和 Windows x64 codegen 后端；其它平台使用 decoded executor。

## Baseline JIT 的运行过程与代码位置

一次函数调用从普通 VM 进入 Baseline JIT，大致经过下面几步：

```text
函数调用或循环回边
  -> 累加热度
  -> 达到阈值，状态 Cold -> Scheduled
  -> 校验并解码 bytecode
  -> 尝试生成 native stub
       -> 成功：保存机器码入口
       -> 失败：构建 decoded baseline code
  -> 状态 Scheduled -> Compiled
  -> 下一次函数调用进入 native stub 或 decoded executor
  -> runtime helper 复用 VM 语义完成具体操作
```

### 1. 收集函数和循环热度

字节码函数在 `JitFeedback` 中保存 `callCount`、`backedgeCount`、`baselineEntryCount`、当前 `JitState`、编译结果和失败原因。函数调用进入 `recordFunctionCall()`，循环执行 `Loop` opcode 时进入 `recordLoopBackedge()`；两者都会使用饱和计数并调用 `maybeScheduleJit()`。

相关代码：

- [include/minijs/bytecode_function.h](../include/minijs/bytecode_function.h)：`JitState`、`JitFeedback`、`BytecodeFunction::jit`
- [include/minijs/vm.h](../include/minijs/vm.h)：`JitOptions`、`setJitEnabled()`、各项阈值设置和调试入口
- [src/vm.cpp](../src/vm.cpp)：`recordFunctionCall()`、`recordLoopBackedge()`

### 2. 达到阈值并触发编译

`maybeScheduleJit()` 只处理 `Cold` 函数。当调用次数或循环回边次数达到阈值时，它先把状态改成 `Scheduled`，再立即调用 `compileScheduledJit()`。编译成功后状态变为 `Compiled` 并保存 `BaselineCode`；两条 baseline 编译路径都失败时，状态变为 `Failed` 并记录 `compileError`，后续继续由普通 VM 执行。

需要注意，`callBytecodeClosure()` 在调用 `recordFunctionCall()` 之前就计算了本次调用是否可以进入 baseline。因此，达到阈值的这一次调用只安装编译结果，下一次调用才会进入 baseline。

相关代码：

- [src/vm.cpp](../src/vm.cpp)：`callBytecodeClosure()`、`maybeScheduleJit()`、`compileScheduledJit()`
- [include/minijs/bytecode_function.h](../include/minijs/bytecode_function.h)：`Cold`、`Scheduled`、`Compiled`、`Failed` 状态及 `compileError`

### 3. 校验并解码 bytecode

`compileBaseline()` 不从 AST 重新生成代码，而是直接复用已经存在的 bytecode。native compiler 和 decoded compiler 都先调用 `verifyBytecode()`，确认指令边界、操作数和跳转目标合法，再通过 `decodeInstruction()` 得到结构化的 `DecodedInstruction`。这样 bytecode 仍然是 VM 与 Baseline JIT 共享的唯一语义来源。

相关代码：

- [include/minijs/bytecode_decoder.h](../include/minijs/bytecode_decoder.h)：`DecodedInstruction`、`BytecodeVerificationResult`、解码和校验接口
- [src/bytecode_decoder.cpp](../src/bytecode_decoder.cpp)：`decodeInstruction()`、`verifyBytecode()`
- [src/baseline_jit.cpp](../src/baseline_jit.cpp)：`compileDecodedBaseline()`、baseline 支持的 opcode 集合

### 4. 优先生成 native stub，失败时使用 decoded baseline

#### Native 路径

`compileBaseline()` 首先调用 `BaselineCompiler::compile()`。当前 ARM64 和 Windows x64 后端按 opcode 发射调用 Runtime ABI helper 的机器码，并记录 bytecode offset 到 native offset 的映射。机器码写入完成后，`ExecutableMemory::allocate()` 负责分配内存、复制代码、切换为可执行权限并刷新指令缓存。

具体 `BaselineCompiler::compile()` 大致按以下顺序执行：

1. 校验 bytecode：verifyBytecode(function.chunk);
2. 创建 `BaselineCode`。
3. 初始化两张映射表：

```
bytecodeOffsetToInstructionIndex
bytecodeOffsetToNativeOffset
```

4. 根据 verifier 得到的指令起点，填写：bytecodeOffsetToInstructionIndex
5. 遍历字节码指令，对每条指令调用：decodeInstruction(...)
6. 在发射该指令前记录：

```
bytecode offset → 当前机器码 offset
```

7. 根据 opcode 发射机器码。
8. 所有机器码发射完成后回填分支。
9. 调用`ExecutableMemory::allocate(emitter.bytes(), ...)`分配可执行内存并复制机器码。
10. 设置机器码入口：

```cpp
code->entry =
    reinterpret_cast<BaselineEntry>(code->executableMemory->data());
```

#### Decoded 路径：

如果当前平台没有 native backend，或者函数包含 native compiler 尚未支持的 opcode，`compileBaseline()` 会继续尝试 `compileDecodedBaseline()`。decoded 路径缓存校验后的指令和跳转映射，由 C++ executor 执行；只有 decoded 路径也不支持该函数时，整个 baseline 编译才失败。

1. `verifyBytecode()`。
2. 创建 `BaselineCode`。
3. 初始化 `bytecodeOffsetToInstructionIndex`。
4. 填写 bytecode offset 到指令数组下标的映射。
5. 逐条调用 `decodeInstruction()`。
6. 把结果放入 `BaselineCode::instructions`，供 `executeBaselineCode()` 按指令下标执行。

相关代码：

- [include/minijs/baseline_jit.h](../include/minijs/baseline_jit.h)、[src/baseline_jit.cpp](../src/baseline_jit.cpp)：`compileBaseline()` 和 native 到 decoded 的回退顺序
- [include/minijs/baseline_compiler.h](../include/minijs/baseline_compiler.h)、[src/baseline_compiler.cpp](../src/baseline_compiler.cpp)：`BaselineCompiler::compile()`、ARM64 和 Windows x64 emitter
- [include/minijs/executable_memory.h](../include/minijs/executable_memory.h)、[src/executable_memory.cpp](../src/executable_memory.cpp)：`ExecutableMemory::allocate()` 和平台内存管理
- [include/minijs/baseline_code.h](../include/minijs/baseline_code.h)：decoded 指令、offset 映射、可执行内存和 `BaselineEntry`

### 5. 下一次调用进入 baseline

函数处于 `Compiled` 状态且 JIT 开启时，`callBytecodeClosure()` 会增加 `baselineEntryCount`。如果 `BaselineCode::entry` 是当前平台可执行的 native 入口，就调用 `executeBaselineEntry()`；否则调用 `executeBaselineCode()` 解释缓存的 decoded 指令。关闭 JIT 后，即使函数已经编译，也会继续走普通 VM。

相关代码：

- [src/vm.cpp](../src/vm.cpp)：`callBytecodeClosure()`、`canExecuteNativeBaselineEntry()`、`executeBaselineEntry()`、`executeBaselineCode()`
- [include/minijs/baseline_code.h](../include/minijs/baseline_code.h)：`BaselineCode::entry` 和两类 offset 映射

### 6. native stub 通过 Runtime ABI 复用 VM 语义

native 入口接收一个 `BaselineFrame*`。frame 保存 VM、当前闭包、局部槽起点、返回槽、栈地址快照和执行结果状态。stub 不依赖 `std::vector<Value>` 的内部布局；Windows x64 的数字快路径通过 frame 的栈快照和 `Value` 提供的字段偏移直接访问数字载荷，其余操作调用 `extern "C"` helper。helper 再转发到 `VM::baselineRuntime*()`，完成完整语义。

helper 用 `bool` 表示成功或失败，并通过 `BaselineFrame::failed`、`completed` 把状态带回 C++。这样异常和复杂 C++ 对象都不会跨越 native ABI 边界。

相关代码：

- [include/minijs/baseline_frame.h](../include/minijs/baseline_frame.h)：`BaselineFrame`、`BaselineEntry`
- [include/minijs/baseline_runtime.h](../include/minijs/baseline_runtime.h)、[src/baseline_runtime.cpp](../src/baseline_runtime.cpp)：稳定的 C ABI helper
- [include/minijs/vm.h](../include/minijs/vm.h)、[src/vm.cpp](../src/vm.cpp)：`baselineRuntimePushConstant()`、`baselineRuntimeGetLocal()`、局部变量/栈 helper、算术 helper、控制流 helper、`baselineRuntimeReturn()` 等 VM 实现

### 7. 反馈信息和测试

属性访问、属性写入和方法调用产生的 inline cache 信息保存在 `Chunk` 的 `FeedbackSlot` 中。目前 native stub 还没有据此做类型特化，但这些反馈可以作为后续 Optimize JIT 的输入。现有测试覆盖热度阈值、状态转换、native code object、decoded executor、Runtime ABI helper dispatch、失败回退和 JIT 开关行为。

相关代码：

- [include/minijs/chunk.h](../include/minijs/chunk.h)、[inline-cache.md](inline-cache.md)：`FeedbackSlot` 和 inline cache 设计
- [tests/test_bytecode.cpp](../tests/test_bytecode.cpp)：JIT、baseline executor、native entry 和 runtime helper 测试

## 核心原则

Baseline JIT 复用现有 bytecode，不维护第二套 opcode 或第二套栈式 IR。

```text
Stable Bytecode
  -> Interpreter VM
  -> Baseline JIT: DecodedInstruction / Opcode -> executor or native code
  -> Optimizing JIT: Bytecode + feedback -> SSA IR -> optimized code
```

Baseline code object 的职责是缓存已经验证过的 bytecode 视图，减少重复 decode，并为后续 dispatch/native codegen 提供稳定输入。Baseline 已使用局部数字类型 guard；需要跨指令类型特化和可恢复执行的 Deopt 时，再进入 Optimizing JIT 的 SSA IR。

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
  Value* stackData = nullptr;
  std::size_t stackSize = 0;

  bool returnsReceiver = false;
  bool completed = false;
  bool failed = false;
  std::string errorMessage;
};

using BaselineEntry = void (*)(BaselineFrame*);
```

机器码通过 Runtime ABI helper 读写局部变量、压栈和返回；部分数字运算可以在 guard 成功后内联。helper 接口例如：

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

`compileBaseline()` 会先尝试第一版 native `BaselineCompiler`。如果函数只包含局部变量、栈清理、全局变量、数组/对象、closure/upvalue、class/method/inherit、普通调用、方法调用、`super` 调用、算术/比较/逻辑 opcode、基础控制流和 `Return`，会在当前平台生成 native stub、`bytecodeOffsetToNativeOffset` 映射和 `BaselineEntry`。如果 native compiler 遇到暂不支持的 opcode 或当前平台没有 native backend，会回退到现有 decoded baseline compiler；这样已经支持的 runtime helper opcode 仍能通过 decoded executor 验证语义。

## 函数职责

`BaselineCompiler::compile()` 是 native baseline 的唯一公开入口。它不抛出“unsupported opcode”异常，而是通过 `BaselineCompileResult::error` 返回失败原因；外层 `compileBaseline()` 可以据此回退到 decoded baseline executor。

`Arm64Emitter` 和 `WindowsX64Emitter` 目前只是 `BaselineCompiler` 私有的极小指令写入器。它们负责各自平台的 prologue/epilogue、helper 调用、branch placeholder 和 branch patch，不承担通用 assembler 的职责。

`emitRuntimeCall()` 固定使用 Runtime ABI，但具体寄存器由后端决定。ARM64 会把 `BaselineFrame*` 放回 `x0`，把 helper 地址装入 `x16`，通过 `blr x16` 调用；Windows x64 会把 `BaselineFrame*` 放回 `rcx`，把 helper 地址装入 `rax`，通过 `call rax` 调用。helper 的 `bool` 返回值随后由平台对应的条件跳转检查，失败时进入 epilogue。

`ExecutableMemory::allocate()` 负责 W^X 生命周期：先申请可写内存并复制机器码，刷新 instruction cache，然后切换为只读可执行内存。释放逻辑集中在 `ExecutableMemory::release()`，供析构和 move assignment 共用。

## 平台后端边界

当前代码已有第一版 ARM64 和 Windows x64 native stub backend，但 Baseline JIT 的上层设计不应该绑定到某一种机器码。稳定边界应该是：

```text
Bytecode / DecodedInstruction
  -> BaselineCompiler
  -> 平台相关 codegen backend
  -> BaselineEntry
```

不同平台可以生成不同机器码，但都应该遵守同一套 Runtime ABI 和 `BaselineFrame` 入口协议：

```cpp
using BaselineEntry = void (*)(BaselineFrame*);
```

因此未来可以继续增加：

| 后端 | 目标 | 当前状态 |
| --- | --- | --- |
| ARM64 | Apple Silicon、Windows/Linux ARM64 | 已有第一版 stub compiler |
| Windows x64 / x86-64 | Windows 桌面平台 | 已有第一版 stub compiler |
| SysV x64 / x86-64 | Linux/macOS x64 平台 | 不实现                   |
| RISC-V 64 | 教学或实验平台 | 不实现                   |
| 其它后端 | 例如 WASM、解释型 threaded code | 不实现 |

后端之间可以共享：

- bytecode decoder 和 verifier
- `BaselineCode`、`DecodedInstruction` 和 offset 映射结构
- Runtime ABI helper，例如 `minijsBaselineAdd()`
- VM dispatch 规则：有 native `BaselineEntry` 就进机器码，否则回退 decoded executor

后端之间必须各自实现：

- prologue / epilogue
- 参数寄存器或调用约定
- helper 地址加载方式
- helper 返回值检查
- 分支 patch 和 native offset 映射
- executable memory 的平台细节

也就是说，文档里的某个平台指令只是“这个后端怎么做”的具体例子，不代表 MiniJS 只能生成一种机器码。许多 opcode 仍然在 ARM64 和 Windows x64 后端都映射到 runtime helper，只是机器码分别使用 ARM64 的 `mov`/`blr`/`cbz` 和 Windows x64 的 `mov`/`call`/`test`/`jz`。

## 当前 Opcode 到 native stub 的映射

第一版 native baseline compiler 大部分 opcode 还没有把 `Value` 运算完全内联到机器码里。它主要生成一层很薄的平台 stub：

```text
Opcode
  -> native stub
  -> Runtime ABI helper
  -> helper 操作 VM stack_
```

也就是说，baseline JIT 里仍然有大量 opcode 不是直接展开完整语义，而是生成一段调用 runtime helper 的机器码。这样可以先验证 native entry、可执行内存、调用约定和错误返回路径，同时避免机器码直接依赖复杂 C++ 对象布局；随后再把足够简单、收益明确的 number-only 场景逐步内联。

Windows x64 的 `OP_NEGATE`、`OP_ADD`、`OP_SUB`、`OP_MUL`、`OP_DIV` 和 `OP_MOD` 已经开始走更靠近真正机器码的路径：stub 先读取 `BaselineFrame` 中缓存的 VM 栈地址和栈深度，检查相关 `Value` 的类型标签是否为 `Number`；命中时直接修改 double 载荷，或者进入 number-only helper，未命中时再回退到对应 runtime helper。这是第一批“number fast path + helper fallback”的 native opcode。

### Helper-call stub 和 fast path 的区别

目前 native baseline 里有两种机器码形态。

第一种是 helper-call stub。它确实会发射机器码，但这段机器码只负责按平台 ABI 设置参数、调用 C ABI helper、检查 helper 的 `bool` 返回值。例如某个 helper-only opcode 在 Windows x64 上大致生成：

```text
mov rcx, rbx      ; BaselineFrame*
mov rax, helper   ; minijsBaselineXxx
call rax
test eax, eax
jz epilogue
```

这不是“没有生成机器码”，而是“生成了调用 runtime helper 的机器码”。优点是语义复用 VM，能安全处理字符串拼接、GC 字符串分配、错误传播和栈变化；缺点是每个 opcode 仍有一次 C++ helper 调用成本。

第二种是 native fast path。它把某个高频、容易验证的子场景直接写进机器码，只在 guard 失败时回退 helper。Windows x64 的 `OP_NEGATE` number 路径大致是：

```text
读取 frame.stackSize
  -> 栈为空：fallback
定位 frame.stackData[stackSize - 1]
检查 Value::type 是否为 Number
  -> 不是 Number：fallback
xor 栈顶 number 的符号位
返回成功
fallback:
  call minijsBaselineNegate(frame)
```

也就是说，`-value` 有两条路径：

- `value` 是 number：机器码直接修改栈顶 `Value::number_`。
- `value` 不是 number：进入 `minijsBaselineNegate(frame)`，继续由 VM 语义产生 `RuntimeError: value is not a number`。

Windows x64 的 `OP_ADD` / `OP_SUB` / `OP_MUL` / `OP_DIV` number 路径会多做一步栈高度同步：

```text
读取 frame.stackSize
  -> 少于 2 个值：fallback
定位 left = frame.stackData[stackSize - 2]
定位 right = frame.stackData[stackSize - 1]
检查 left/right 的 Value::type 是否都是 Number
  -> 任意一个不是 Number：fallback
如果是 OP_DIV，检查 right 是否为 0
  -> right 是 0：fallback
left.number = left.number <op> right.number
frame.stackSize--
call minijsBaselineSyncStackSize(frame)
fallback:
  call minijsBaselineSub(frame) / minijsBaselineMul(frame) / minijsBaselineDiv(frame)
```

`OP_NEGATE` 只改栈顶值，不改变栈高度；`OP_ADD`、`OP_SUB`、`OP_MUL` 和 `OP_DIV` 会把两个操作数合成一个结果，所以 native 代码先递减 `frame.stackSize`，再调用 `minijsBaselineSyncStackSize(frame)` 让真实的 `VM::stack_` resize 到同一个长度。这个 helper 不负责做算术，只负责把 native fast path 已经完成的栈形状同步回 VM。`OP_ADD` 只优化 `number + number`，因为 `+` 还承载字符串拼接语义；只要任意一边不是 number，就回退到 `minijsBaselineAdd(frame)`。`OP_DIV` 还会在执行 `divsd` 前检查除数；如果除数是 `0`，它会回退到 `minijsBaselineDiv(frame)`，继续由 VM 抛出 `RuntimeError: division by zero`。

`OP_MOD` 也会先在 native 里检查两个操作数都是 number，并检查右操作数不是 `0`。不过 x64 SSE 没有直接等价的 `modsd`，所以 guard 成功后它调用更窄的 `minijsBaselineModNumber(frame)`；guard 失败或右操作数为 `0` 时仍回退到 `minijsBaselineMod(frame)`，保留 `value is not a number` 和 `modulo by zero`。

这就是后续优化的模板：每次只内联一个足够简单的 number-only fast path，并保留 helper fallback 作为语义兜底。

### Windows x64 数字比较快路径

`OP_GREATER`、`OP_LESS` 和 `OP_EQUAL` 先检查栈深度及两个操作数的 Number 标签。guard 未命中时调用原来的完整 helper，保留类型错误或非数字相等语义；成功时直接比较 double，把左操作数槽改为 Boolean，再减少一个栈槽并调用 `minijsBaselineSyncStackSize()`。

```asm
movsd xmm0, [rdx + Value::numberOffset]   ; rdx 指向左操作数
ucomisd xmm0, [rcx + Value::numberOffset] ; rcx 指向右操作数
```

大于比较使用：

```asm
seta al                                 ; CF=0 且 ZF=0
```

小于比较使用：

```asm
setb al                                 ; CF=1
setnp cl                                ; PF=0，比较结果必须有序
and al, cl                              ; 排除 NaN 的 unordered 情况
```

相等比较使用：

```asm
sete al                                 ; ZF=1，包括 unordered 情况
setnp cl                                ; PF=0，排除 NaN
and al, cl                              ; 只有有序且相等时为 true
```

任一操作数为 NaN 时，`ucomisd` 会设置 CF、ZF、PF，因此 `seta` 自然得到 false，而小于和相等比较需要额外用 `setnp` 排除 NaN。`+0` 和 `-0` 比较相等，两种严格比较都为 false，相等比较为 true；无穷大遵循 double 比较规则。这与 `Value::equals()` 使用的 double `==` 一致。

结果写回时分别更新 `Value::booleanOffset()` 和 `Value::typeOffset()`。小于路径中的 `setnp cl` 会改写 rcx 的低字节，此时右操作数地址已不再使用；左操作数地址仍保存在 rdx。两个输入经过 Number guard，不持有需要释放的对象资源，因此可以原地转换为 Boolean。

测试直接调用 native entry，将普通数字、相等值、正负零、无穷大和左右 NaN 与 decoded executor 对比，并检查两侧非数字 fallback。JIT on/off 测试同时覆盖比较后的条件跳转、循环、数组结果和嵌套 helper 调用。

### BaselineFrame 的栈快照

`BaselineFrame` 除了保存 VM、闭包、`returnSlot`、`slotStart` 等调用信息外，现在还保存：

```cpp
Value* stackData;
std::size_t stackSize;
```

这两个字段不是新的 VM 栈，也不拥有任何值；它们只是当前 `VM::stack_` 的快照，供 native fast path 用固定偏移访问栈顶。因为 helper 可能 `push()` / `pop()`，导致 `std::vector<Value>` 重新分配，所以每个 baseline runtime helper 成功修改栈后都会调用 `refreshBaselineFrameStack(frame)` 更新快照。对于 `OP_ADD` / `OP_SUB` / `OP_MUL` / `OP_DIV` 这种会在 native fast path 里改变栈高度的 opcode，还会调用 `minijsBaselineSyncStackSize(frame)` 把 `frame.stackSize` 同步回真实的 `VM::stack_`。`OP_MOD` 的栈高度变化由 `minijsBaselineModNumber(frame)` 内部完成，所以它复用普通 helper 的刷新机制。

这样 native fast path 不需要直接理解 `std::vector<Value>` 的内部布局，只需要读 `BaselineFrame` 中已经刷新过的 `stackData` 和 `stackSize`。

### Value 布局 offset

Windows x64 number fast path 需要判断 `Value` 是否为 number，并直接修改 number 载荷。因此 `Value` 暴露了几个只读 codegen 辅助接口：

```cpp
Value::typeOffset()
Value::numberOffset()
Value::booleanOffset()
Value::numberTypeTag()
Value::booleanTypeTag()
```

这些接口只用于 native baseline codegen。普通 VM 和 runtime helper 仍然应该通过 `isNumber()`、`asNumber()`、`Value(double)` 等正常接口访问 `Value`。如果未来 `Value` 布局变化，fast path 通过这些 offset 自动拿到新位置，而不是在机器码生成器里散落硬编码偏移。

### ARM64 stub 形态

ARM64 下 `BaselineFrame*` 会作为第一个参数通过 `x0` 传入。每个 native baseline 函数统一包含：

```text
prologue:
  stp x29, x30, [sp, #-16]!  // 保存帧指针和返回地址，同时分配 16 字节栈空间
  mov x29, sp                 // 建立当前 native stub 的栈帧
  stp x19, x20, [sp, #-16]!  // 保存 callee-saved 寄存器，并保持栈 16 字节对齐
  mov x19, x0                 // 将 BaselineFrame* 固定保存在 x19，供后续 helper 复用

body:
  按 Opcode 顺序调用 runtime helper

epilogue:
  ldp x19, x20, [sp], #16    // 恢复 callee-saved 寄存器并回收对应栈空间
  ldp x29, x30, [sp], #16    // 恢复帧指针和返回地址
  ret                         // 返回 VM 的 C++ 调用入口
```

`x0` 是进入 native baseline 时传入的 `BaselineFrame*`。由于 helper 调用会使用 `x0` 和 `x1` 传参，prologue 会先把 frame 固定保存到 callee-saved 寄存器 `x19`。之后每次调用 helper 前，再把 `x19` 复制回 `x0`。

当前支持的 opcode 映射如下：

| Opcode | 操作数 | Runtime helper | 结果 |
| --- | --- | --- | --- |
| `OP_CONSTANT` | `constantIndex` | Windows x64 数字常量内嵌 + `minijsBaselinePushNumberSlot(frame)`，否则 `minijsBaselinePushConstant(frame, constantIndex)` | 数字在编译时复制位编码，运行时构造栈槽后直接写载荷；非数字保留完整 helper |
| `OP_GET_LOCAL` | `slot` | Windows x64 Number guard + `minijsBaselinePushNumberSlot(frame)`，否则 `minijsBaselineGetLocal(frame, slot)` | 窄 helper 构造压栈槽并刷新地址，机器码复制数字载荷；非数字或越界走完整 helper |
| `OP_SET_LOCAL` | `slot` | Windows x64 number fast path，否则 `minijsBaselineSetLocal(frame, slot)` | 目标局部槽和栈顶都是数字时直接复制 double；类型变化走完整 Value 赋值；栈顶保留 |
| `OP_POP` | 无 | `minijsBaselinePop(frame)` | 使用 `discardTop()` 直接销毁栈顶 Value，不构造无用副本，再刷新栈快照 |
| `OP_DEFINE_GLOBAL` | `nameIndex` | `minijsBaselineDefineGlobal(frame, nameIndex)` | 从当前函数常量池读取变量名，把栈顶值弹出并写入 `globals_` |
| `OP_GET_GLOBAL` | `nameIndex` | `minijsBaselineGetGlobal(frame, nameIndex)` | 从 `globals_` 读取变量并压栈；变量不存在时保留 VM 的运行时错误 |
| `OP_SET_GLOBAL` | `nameIndex` | `minijsBaselineSetGlobal(frame, nameIndex)` | 把栈顶值写入已有全局变量；变量不存在时保留 VM 的运行时错误，栈顶值不弹出 |
| `OP_ARRAY` | `count` | `minijsBaselineArray(frame, count)` | 弹出 `count` 个元素，创建 VM 管理的 GC 数组并压栈 |
| `OP_GET_INDEX` | 无 | `minijsBaselineGetIndex(frame)` | 弹出数组和下标，读取元素后压栈；类型和越界错误沿用 VM 语义 |
| `OP_SET_INDEX` | 无 | `minijsBaselineSetIndex(frame)` | 弹出数组、下标和值，写入元素后把赋值结果压栈 |
| `OP_OBJECT` | `namesIndex` | `minijsBaselineObject(frame, namesIndex)` | 从常量池读取属性名数组，弹出对应属性值，创建 GC 对象并压栈 |
| `OP_GET_PROPERTY` | `nameIndex, feedbackSlot` | `minijsBaselineGetProperty(frame, nameIndex, feedbackSlot)` | 弹出对象并读取属性；普通对象继续复用属性 inline cache |
| `OP_SET_PROPERTY` | `nameIndex, feedbackSlot` | `minijsBaselineSetProperty(frame, nameIndex, feedbackSlot)` | 写入对象属性并把赋值结果压栈；普通对象继续复用属性 inline cache |
| `OP_CLASS` | `nameIndex` | `minijsBaselineClass(frame, nameIndex)` | 从常量池读取类名，创建 GC class 对象并压栈 |
| `OP_METHOD` | `nameIndex` | `minijsBaselineMethod(frame, nameIndex)` | 弹出方法 closure，把它写入栈顶 class 的实例方法表 |
| `OP_STATIC_METHOD` | `nameIndex` | `minijsBaselineStaticMethod(frame, nameIndex)` | 弹出方法 closure，把它写入栈顶 class 的静态方法表 |
| `OP_INHERIT` | 无 | `minijsBaselineInherit(frame)` | 弹出 superclass，把栈顶 subclass 的 superclass 指针指向它，并保留 subclass 在栈顶 |
| `OP_GET_UPVALUE` | `slot` | `minijsBaselineGetUpvalue(frame, slot)` | 读取当前闭包的 upvalue，打开 upvalue 从 VM 栈读取，关闭 upvalue 从 closed 值读取 |
| `OP_SET_UPVALUE` | `slot` | `minijsBaselineSetUpvalue(frame, slot)` | 把栈顶值写入当前闭包的 upvalue，栈顶值保留给后续表达式或 `OP_POP` |
| `OP_CLOSE_UPVALUE` | 无 | `minijsBaselineCloseUpvalue(frame)` | 关闭当前栈顶槽关联的 open upvalue，并弹出该局部槽 |
| `OP_GET_CURRENT_CLOSURE` | 无 | `minijsBaselineGetCurrentClosure(frame)` | 把当前运行中的 closure 压栈，用于局部递归函数引用自身 |
| `OP_CLOSURE` | `functionIndex` + upvalue 元数据 | `minijsBaselineClosure(frame, functionIndex, bytecodeOffset)` | 从常量池函数模板创建运行时 closure，并从当前 bytecode 指令尾部读取 `(isLocal, index)` 元数据捕获 upvalue |
| `OP_CALL` | `argCount` | `minijsBaselineCall(frame, argCount)` | 按普通 VM 调用语义消费 callee 和参数；native function 直接调用，bytecode closure 会同步进入其 baseline code 并把返回值写回调用位置 |
| `OP_METHOD_CALL` | `nameIndex, argCount, feedbackSlot` | `minijsBaselineMethodCall(frame, nameIndex, argCount, feedbackSlot)` | 读取 receiver 的方法或数组内建方法；实例/静态方法复用 method inline cache，并同步进入目标方法 closure |
| `OP_SUPER_CALL` | `nameIndex, argCount` | `minijsBaselineSuperCall(frame, nameIndex, argCount)` | 从 superclass 查找方法，把栈上的 `[superclass, receiver, args...]` 重排成 `[receiver, args...]`，再同步进入父类方法 closure |
| `OP_ADD` | 无 | Windows x64 number fast path，否则 `minijsBaselineAdd(frame)` | 两个操作数都是数字时直接执行 double 加法并同步栈高度；否则回退 helper 保留字符串拼接语义 |
| `OP_SUB` | 无 | Windows x64 number fast path，否则 `minijsBaselineSub(frame)` | 两个操作数都是数字时直接执行 double 减法并同步栈高度；否则回退 helper 保留 VM 语义 |
| `OP_MUL` | 无 | Windows x64 number fast path，否则 `minijsBaselineMul(frame)` | 两个操作数都是数字时直接执行 double 乘法并同步栈高度；否则回退 helper 保留 VM 语义 |
| `OP_DIV` | 无 | Windows x64 number fast path，否则 `minijsBaselineDiv(frame)` | 两个操作数都是数字且除数非 0 时直接执行 double 除法并同步栈高度；否则回退 helper 保留 VM 语义 |
| `OP_MOD` | 无 | Windows x64 guarded number helper，否则 `minijsBaselineMod(frame)` | 两个操作数都是数字且除数非 0 时调用 `minijsBaselineModNumber(frame)`；否则回退 helper 保留 VM 语义 |
| `OP_NEGATE` | 无 | Windows x64 number fast path，否则 `minijsBaselineNegate(frame)` | 栈顶是数字时直接翻转符号位；否则回退 helper 保留 VM 语义 |
| `OP_EQUAL` | 无 | Windows x64 number fast path，否则 `minijsBaselineEqual(frame)` | 数字 guard 成功时通过 `ucomisd` / `sete` / `setnp` 写回 Boolean 并同步栈；否则通过 `Value::equals()` 保留字符串、布尔值和引用身份比较语义 |
| `OP_GREATER` | 无 | Windows x64 number fast path，否则 `minijsBaselineGreater(frame)` | 数字 guard 成功时通过 `ucomisd` / `seta` 写回 Boolean 并同步栈；否则回退完整 helper |
| `OP_LESS` | 无 | Windows x64 number fast path，否则 `minijsBaselineLess(frame)` | 数字 guard 成功时通过 `ucomisd` / `setb` / `setnp` 写回 Boolean 并同步栈，NaN 结果为 false；否则回退完整 helper |
| `OP_NOT` | 无 | Windows x64 Boolean fast path，否则 `minijsBaselineNot(frame)` | Boolean 原地取反；其它类型通过 `isTruthy()` 取反后替换为布尔结果；栈深度不变 |
| `OP_JUMP_IF_FALSE` | `jumpOffset` | Windows x64 Boolean fast path，否则 `minijsBaselinePeekTruthy(frame)`；随后 native conditional branch | 读取栈顶 truthiness，不弹栈；返回 falsey 时跳到 bytecode 目标对应的 native offset |
| `OP_JUMP` | `jumpOffset` | native unconditional branch | 无条件跳到 bytecode 目标对应的 native offset |
| `OP_LOOP` | `jumpOffset` | `minijsBaselineRecordLoopBackedge(frame)` + native unconditional branch | 记录循环回边热度，然后跳回 loop 目标对应的 native offset |
| `OP_RETURN` | 无 | `minijsBaselineReturn(frame)` | 弹出返回值，关闭 upvalue，把结果写回 `returnSlot`，标记 `completed` |

对应的 ARM64 stub 形态如下。

`OP_CONSTANT constantIndex`：

```text
mov x0, x19                 // 第一个参数：BaselineFrame*
mov w1, constantIndex       // 第二个参数：32 位常量池索引
mov x16, helper             // 将 minijsBaselinePushConstant 地址装入临时寄存器
blr x16                     // 间接调用 helper，返回地址写入 x30
cbz w0, epilogue            // bool 返回值为 false 时跳到统一出口
```

`OP_GET_LOCAL slot`：

```text
mov x0, x19                 // 第一个参数：BaselineFrame*
mov w1, slot                // 第二个参数：32 位局部变量槽索引
mov x16, helper             // 将 minijsBaselineGetLocal 地址装入临时寄存器
blr x16                     // 调用 helper，将局部变量值压入 VM 栈
cbz w0, epilogue            // helper 失败时立即退出 native stub
```

`OP_ADD`：

```text
mov x0, x19                 // 唯一参数：BaselineFrame*
mov x16, helper             // 将 minijsBaselineAdd 地址装入临时寄存器
blr x16                     // helper 完成出栈、相加或拼接、结果入栈
cbz w0, epilogue            // helper 返回 false 表示 frame 中已记录失败
```

`OP_RETURN`：

```text
mov x0, x19                 // 唯一参数：BaselineFrame*
mov x16, helper             // 将 minijsBaselineReturn 地址装入临时寄存器
blr x16                     // 写回返回值并把 frame 标记为 completed
cbz w0, epilogue            // 失败时退出
b epilogue                  // 成功返回后也结束 stub，不再执行后续 bytecode
```

其中 `mov x16, helper` 不是单条源码级函数，而是由 `emitLoadX16Imm64()` 生成的一组 `movz`/`movk`，把 64 位 helper 地址装入 `x16`：

```text
movz x16, low16             // 写入地址的最低 16 位，并将其余位清零
movk x16, next16, lsl #16   // 保留已有位，补入地址的第 16..31 位
movk x16, next16, lsl #32   // 补入地址的第 32..47 位
movk x16, next16, lsl #48   // 补入地址的第 48..63 位
blr  x16                    // 跳转到完整的 64 位 helper 地址并保存返回地址
```

如果 helper 返回 `false`，机器码通过 `cbz w0, epilogue` 直接跳到统一出口。回到 C++ 后，`VM::executeBaselineEntry()` 会检查：

```cpp
frame.failed
frame.completed
stack_.empty()
```

并把失败转换成 `RuntimeError`。因此 C++ 异常不会跨过 native ABI 边界。

`OP_RETURN` 额外发射一条无条件跳转到 epilogue，是因为返回成功后函数已经完成，不应该继续执行后续机器码。`minijsBaselineReturn()` 会根据 `BaselineFrame::returnsReceiver` 保持构造器 `init` 的 receiver 返回语义。

`OP_JUMP_IF_FALSE` 没有复用普通 helper 的 `bool` 返回值表示条件真假，因为 baseline runtime helper 的 `bool` 已经约定为“helper 是否成功”。为避免把“条件为 false”和“helper 失败”混在一起，`minijsBaselinePeekTruthy()` 返回一个三态整数：`0` 表示栈顶为 falsey，`1` 表示栈顶为 truthy，`2` 表示 helper 失败且 `frame.failed` 已设置。native stub 看到 `0` 时跳到 bytecode 分支目标，看到 `1` 时继续执行下一条指令，看到其他值时跳到 epilogue。

`OP_JUMP` 和 `OP_LOOP` 的目标不是运行时计算出来的；bytecode verifier 已经确认目标落在指令边界上，native compiler 先记录 bytecode target，等所有指令都生成完并填好 `bytecodeOffsetToNativeOffset` 后，再把占位跳转回填成真正的 native 相对分支。`OP_LOOP` 在跳回去前仍会调用 `minijsBaselineRecordLoopBackedge()`，保持循环热度统计和普通 VM 一致。

`BaselineCode::bytecodeOffsetToNativeOffset` 会记录每条 bytecode 指令对应的机器码起点，当前用于分支回填、测试和调试。后续 safepoint、deopt 或源码级 profiling 可以以它为定位依据，但仍需单独补充恢复状态等元数据。

### Windows x64 stub 形态

Windows x64 下 `BaselineFrame*` 会作为第一个参数通过 `rcx` 传入，第二个整数参数使用 `rdx` / `edx`。Windows x64 调用约定要求 caller 为被调用函数预留 32 字节 shadow space，因此 prologue 会保存 `rbx` 并分配 shadow space：

```text
prologue:
  push rbx                   ; 保存 callee-saved rbx
  sub rsp, 32                ; 为被调用 helper 预留 Windows x64 shadow space
  mov rbx, rcx               ; 将 BaselineFrame* 固定保存在 rbx

body:
  按 Opcode 顺序调用 runtime helper

epilogue:
  add rsp, 32                ; 回收 shadow space
  pop rbx                    ; 恢复调用者的 rbx
  ret                        ; 返回 VM 的 C++ 调用入口
```

`rcx` 是进入 native baseline 时传入的 `BaselineFrame*`。由于 helper 调用会使用 `rcx` 和 `rdx` 传参，prologue 会先把 frame 固定保存到 callee-saved 寄存器 `rbx`。之后每次调用 helper 前，再把 `rbx` 复制回 `rcx`。

Windows x64 使用同一张 opcode/helper 映射表，但 stub 指令形态不同。

`OP_CONSTANT constantIndex`：

```text
mov rcx, rbx                 ; 第一个参数：BaselineFrame*
mov edx, constantIndex       ; 第二个参数：32 位常量池索引
mov rax, helper              ; 将 minijsBaselinePushConstant 的 64 位地址装入 rax
call rax                     ; 间接调用 helper
test eax, eax                ; 检查 bool 返回值是否为零
jz epilogue                  ; false 表示失败，跳到统一出口
```

`OP_GET_LOCAL slot`：

```text
mov rcx, rbx                 ; 第一个参数：BaselineFrame*
mov edx, slot                ; 第二个参数：32 位局部变量槽索引
mov rax, helper              ; 将 minijsBaselineGetLocal 的地址装入 rax
call rax                     ; 调用 helper，将局部变量值压入 VM 栈
test eax, eax                ; 检查 helper 的 bool 返回值
jz epilogue                  ; helper 失败时退出 native stub
```

`OP_ADD`：

```text
mov rcx, rbx                 ; 唯一参数：BaselineFrame*
mov rax, helper              ; 将 minijsBaselineAdd 的地址装入 rax
call rax                     ; helper 完成出栈、相加或拼接、结果入栈
test eax, eax                ; 检查 helper 的 bool 返回值
jz epilogue                  ; false 表示 frame 中已记录失败
```

`OP_RETURN`：

```text
mov rcx, rbx                 ; 唯一参数：BaselineFrame*
mov rax, helper              ; 将 minijsBaselineReturn 的地址装入 rax
call rax                     ; 写回返回值并把 frame 标记为 completed
test eax, eax                ; 检查 helper 的 bool 返回值
jz epilogue                  ; 失败时退出
jmp epilogue                 ; 成功返回后也结束 stub，不再执行后续 bytecode
```

其中 `mov rax, helper` 由 `WindowsX64Emitter::emitLoadHelper()` 写入 `mov rax, imm64`。helper 返回值位于 `eax`；机器码通过 `test eax, eax` 和 `jz epilogue` 处理失败路径。`OP_RETURN` 成功后额外发射无条件跳转，避免继续执行后续机器码。

## 当前支持范围

第四阶段先支持低风险 opcode：

- 纯表达式、局部变量、全局变量、数组下标、对象属性和闭包访问：`Constant`、`Add`、`Sub`、`Mul`、`Div`、`Mod`、`Negate`、`Equal`、`Greater`、`Less`、`Not`、`GetLocal`、`SetLocal`、`Pop`、`DefineGlobal`、`GetGlobal`、`SetGlobal`、`Array`、`GetIndex`、`SetIndex`、`Object`、`GetProperty`、`SetProperty`、`GetUpvalue`、`SetUpvalue`、`CloseUpvalue`、`GetCurrentClosure`、`Return`
- 控制流：`JumpIfFalse`、`Jump`、`Loop`

第六阶段继续支持不引入新调用帧的 runtime helper opcode：

当前这些 opcode 已经迁移到 native compiler。`OP_CLOSURE` 通过 `minijsBaselineClosure()` 创建运行时 closure 并捕获 upvalue；`OP_CLASS`、`OP_METHOD`、`OP_STATIC_METHOD` 和 `OP_INHERIT` 通过各自 helper 复用 VM 的 class 构造语义；普通函数调用通过 `minijsBaselineCall()` 复用 VM 调用语义，方法调用通过 `minijsBaselineMethodCall()` 复用 method inline cache 和数组内建方法路径，`super` 调用通过 `minijsBaselineSuperCall()` 复用 VM 的 superclass 查找和栈槽重排逻辑。后续扩展时优先直接为现有 `Opcode` 增加执行路径或 runtime helper 路径，不新增平行 opcode。

第七阶段 native compiler 的平台 backend switch 当前覆盖：

- `Constant`
- `GetLocal`、`SetLocal`、`Pop`
- `DefineGlobal`、`GetGlobal`、`SetGlobal`
- `Array`、`GetIndex`、`SetIndex`
- `Object`、`GetProperty`、`SetProperty`
- `GetUpvalue`、`SetUpvalue`、`CloseUpvalue`、`GetCurrentClosure`
- `Add`、`Sub`、`Mul`、`Div`、`Mod`、`Negate`、`Equal`、`Greater`、`Less`、`Not`
- `JumpIfFalse`、`Jump`、`Loop`
- `Return`

这些指令大多通过 Runtime ABI helper 完成语义；Windows x64 的部分 number fast path 会通过 `BaselineFrame::stackData` 和 `BaselineFrame::stackSize` 访问栈快照。每条普通 helper 调用后的 `false` 返回值都会被修补成跳转到 epilogue，`Return` 成功后也跳到 epilogue。控制流 opcode 会通过 bytecode offset 到 native offset 的映射回填跳转目标。

## 执行模型

Baseline frame 复用 VM 的调用约定：

- 参数仍位于 VM `stack_` 的 callee/receiver 后方。
- `returnSlot` 和 `slotStart` 与 `CallFrame` 保持一致。
- 局部变量仍使用 bytecode slot 编号。
- 返回值仍写回调用方期待的位置。
- GC roots 仍来自 `stack_`、`globals_`、`frames_`、`openUpvalues_` 和 `temporaryRoots_`。

当前 baseline dispatch 已接入 `callBytecodeClosure()` 的函数入口。当前平台支持 native backend 且 `BaselineCode::entry != nullptr` 时，VM 会优先进入 native `BaselineEntry`；否则继续进入 decoded baseline executor。测试入口仍保留，用于直接验证 code object 和 runtime helper ABI。

非调用型 runtime opcode 通过 VM helper 与解释器共享语义，例如 global 读写、数组创建、对象属性读写和 upvalue 读写。baseline 只负责从 `DecodedInstruction::operands` 取操作数，不直接读取或推进 `frame.ip`。

## Benchmark 覆盖

`examples/baseline_jit_benchmark.js` 是当前 baseline JIT 的综合 smoke benchmark。它覆盖：

- 普通函数调用：`callAdd()` 反复调用 `add()`。
- 闭包创建和调用：`makeAdder()` 返回捕获外部变量的 `addInner()`。
- 方法调用、类实例化和静态方法：`Child()`、`child.value()`、`Child.bonus()`。
- 继承和 `super`：`Child.value()` 通过 `super.value()` 进入父类方法。
- 对象属性读写：`point.x` 读取和 `point.y = ...` 写入。
- 数组方法和下标读写：`values.push()`、`values[j]`、`values[j] = ...`、`values.pop()`。

JIT on/off 对比可以直接运行：

```text
minijs --benchmark-compare-jit examples/baseline_jit_benchmark.js
```

如果要做更长时间的采样，可以在 benchmark 命令后追加参数：

```text
minijs --benchmark-compare-jit examples/baseline_jit_benchmark.js --benchmark-samples 20 --benchmark-warmup 5
```

`--benchmark-warmup` 会先运行指定轮数并丢弃结果，`--benchmark-samples` 决定正式采样轮数。`compile_us` 和 `run_us` 输出正式采样的中位数，`samples` / `warmup_samples` 字段显示实际使用的轮数。

如果要给脚本、CI 或后续画图工具消费，可以追加 JSON 输出格式：

```text
minijs --benchmark-compare-jit examples/baseline_jit_benchmark.js --benchmark-format json
```

JSON 输出会保留 `result`、`speedup`、采样轮数、编译/运行耗时、inline cache 统计和 JIT 调度统计。JIT 对比模式下，结果分为 `without_jit` 和 `with_jit` 两个对象；其中 `with_jit.jit.counters.baseline_entries` 可以直接用来确认 benchmark 是否真的进入 baseline。

CTest 也会通过 `minijs_cli_benchmark_compare_reports_jit_on_and_off` 跑同一份脚本，确认 JIT 开关不改变结果，并输出 `without_jit_run_us`、`with_jit_run_us` 和 `speedup`。同一份输出还包含 JIT 调度统计：`jit functions` 显示函数处于 `cold/scheduled/compiled/failed` 的数量，`jit counters` 显示函数调用次数、回边次数和 `baseline_entries`，`jit code` 显示最终进入 native entry 还是 decoded baseline executor。

### 可选 Helper 采样

使用下面的命令观察剩余 native runtime helper 热点，默认不采样：

```text
minijs --benchmark-compare-jit examples/baseline_numeric_benchmark.js --benchmark-helper-profile on --benchmark-samples 3 --benchmark-warmup 1 --benchmark-format json
```

`--benchmark-helper-profile` 接受 `on` 或 `off`；开启仅适用于 `--benchmark-compare-jit`。文本输出增加 helper 名称、`calls` 和 `elapsed_ns`；JSON 在 `jit.helper_profile.helpers` 中输出相同字段，按名称排序。未开启时保留原来的输出格式。

采样只覆盖实际进入的 native C ABI helper，内联成功的 opcode、普通 VM 和 decoded executor 不计入。有效 frame 上的 helper 异常退出也计一次；无效 frame 被入口检查拒绝时不计入。窄 helper（数字槽构造、栈深度同步等）独立列出，不与完整 opcode helper 混淆。

`elapsed_ns` 为 helper 内 VM 操作的包含子调用耗时（inclusive），不含入口检查、异常包装和退出时更新统计容器的耗时，但读时钟本身会影响运行。`Call`、`SuperCall` 等会包含嵌套函数执行，不能把所有项相加当作总耗时，也不能直接用采样模式的 `speedup` 衡量优化收益。短 helper 的计时尤其容易受到采样开销和系统噪声影响，应结合调用次数判断，再关闭采样做前后性能对照。

每轮仍使用新 VM。报告中的 helper 数据来自最后一轮正式采样，不是各轮耗时中位数；预热数据被丢弃。VM 可通过 `setBaselineHelperProfilingEnabled()` 开关采样，通过 `resetBaselineHelperProfile()` 清空数据；关闭只停止新增，原有快照保留。统计分配失败会丢弃该条诊断数据，不改变语言异常或 helper 返回值。

关闭时不读时钟、不更新容器，只保留作用域开关检查。当前采样数据和下一步建议见 [性能报告](baseline-numeric-benchmark.md)。这是开发诊断工具，不是新的 bytecode 或执行语义。

## Native Fast Path 与 Fallback 边界

以下描述当前实现；可恢复执行的 deopt 属于后续设计，尚未实现。

### 三种退出或回退

| 触发点 | 当前行为 | 后续执行位置 |
| --- | --- | --- |
| native 编译失败或平台没有 native backend | `compileBaseline()` 尝试 decoded 编译 | 后续函数调用从 decoded executor 入口开始 |
| 数字快路径的 guard 未命中 | 跳到当前 opcode 的完整 runtime helper | helper 成功后继续下一条 native 指令 |
| runtime helper 抛异常或返回失败 | C ABI 包装记录失败，native 跳到统一 epilogue | `executeBaselineEntry()` 抛出 `RuntimeError` |

如果 decoded 编译也失败，函数进入 `JitState::Failed`，后续调用继续普通 VM。编译回退发生在选择执行入口之前，不涉及从执行到一半的 native 代码恢复。

指令级 fallback 也不会切换到 decoded executor。它仍在同一次 native 函数调用里，只借助 helper 完成当前指令。helper 失败时不能从函数开头重跑：全局写入、属性写入或函数调用可能已经产生副作用，重跑会重复这些操作。

### 当前 opcode 分类

| opcode | Windows x64 | ARM64 |
| --- | --- | --- |
| `Negate` | 检查栈深度和 Number 标签，直接翻转 double 符号位；未命中调用完整 helper | 完整 helper |
| `Add`、`Sub`、`Mul`、`Div` | 检查两个 Number，使用 SSE double 运算；成功后同步栈深度；未命中调用完整 helper | 完整 helper |
| `Mod` | 数字 guard 成功后调用 `minijsBaselineModNumber()`；未命中调用完整 helper | 完整 helper |
| `GetLocal` | Number guard + 数字槽构造 helper + native 载荷复制；其它情况完整 helper | 完整 helper |
| `SetLocal` | 源、目标都是 Number 时 native 载荷复制；其它情况完整 helper | 完整 helper |
| `Constant` | 数字使用槽构造 helper + native 位编码写入；非数字完整 helper | 完整 helper |
| global、array/index、object/property | 完整 helper | 完整 helper |
| closure/upvalue、class/method/inherit、call/method-call/super-call | 完整 helper | 完整 helper |
| `Greater`、`Less` | 数字 guard 成功后使用 `ucomisd` 并写回 Boolean；未命中调用完整 helper | 完整 helper |
| `Equal` | 数字 guard 成功后使用 `ucomisd` / `sete` / `setnp`；未命中调用完整 helper | 完整 helper |
| `Not` | Boolean guard + native 原地取反；其它情况完整 helper | 完整 helper |
| `Jump` | native 分支 | native 分支 |
| `JumpIfFalse` | Boolean guard + native 读取和分支；其它情况 truthiness helper | truthiness helper 加 native 分支 |
| `Loop` | 回边计数 helper 加 native 分支 | 回边计数 helper 加 native 分支 |
| `Return` | 返回 helper 加统一 epilogue | 返回 helper 加统一 epilogue |

`Div` 和 `Mod` 的零除数 guard 同时识别 `+0` 和 `-0`，交给完整 helper 产生现有除零错误；NaN 不按零处理。`ModNumber` 内部仍使用 `std::fmod`，因此它是经过 guard 的窄 helper，还没有完整内联为机器码。

例如 `1 + 2` 命中数字快路径；`"a" + "b"` 的 Number guard 未命中，完整 `Add` helper 执行字符串拼接。guard 失败只说明快路径不适用，是否报错由完整 helper 的语言语义决定。

### Guard 与栈状态契约

1. guard 必须在修改 Value、弹栈或产生其它副作用之前完成。失败时 helper 看到原来的操作数和栈深度。
2. 快路径成功后的栈效果必须与普通 VM 相同。`Negate` 原地修改栈顶，不改变栈深度；二元运算把结果写入左操作数槽，并减少一个栈槽。`Greater` 和 `Less` 还会把左槽的类型标签和布尔载荷改为 Boolean 结果。
3. 二元快路径先减少 `BaselineFrame::stackSize`，随后调用 `minijsBaselineSyncStackSize()`，由 VM 执行 `stack_.resize()`，再刷新栈快照。同步完成之前不能调用可能分配或触发 GC 的语义 helper。
4. `refreshBaselineFrameStack()` 从真实 `stack_` 更新 `stackData` 和 `stackSize`。可能改变栈或重入 VM 的 helper 成功返回后，要确保快照已刷新；不改变栈的 truthiness 和回边计数 helper 不需要刷新。
5. helper 调用后不得继续使用调用前缓存的栈元素地址。压栈或嵌套调用可能使 `std::vector` 扩容，旧 `Value*` 随之失效；下一条快路径重新读取 frame 的快照。

相关实现：`WindowsX64Emitter::emitInlineNumberNegateOrRuntimeCall()`、`emitInlineNumberBinaryOrRuntimeCall()`、`emitGuardedNumberBinaryRuntimeCall()` 位于 [src/baseline_compiler.cpp](../src/baseline_compiler.cpp)；栈同步位于 [src/vm.cpp](../src/vm.cpp)。

### Helper 返回值与错误传播

普通 C ABI helper 返回 `true` 表示操作成功，native 继续执行；捕获异常时设置 `failed` 和可用的 `errorMessage`，返回 `false`，native 跳到 epilogue。epilogue 只恢复机器调用帧，不负责恢复语言栈或重试 opcode。

`Return` 成功时设置 `completed = true`，完成 upvalue 关闭和返回槽整理，也跳到 epilogue。`executeBaselineEntry()` 检查 `failed`、`completed` 和返回值是否存在，失败时抛出错误。

`minijsBaselinePeekTruthy()` 是单独的三态接口：`0` 表示假，`1` 表示真，`2` 表示失败。`JumpIfFalse` 对 `0` 跳到 bytecode 目标，对 `1` 继续，对 `2` 退出，不能按普通 bool helper 处理。

### GC 与 Safepoint 边界

当前数字内联路径不会分配 GC 对象。可能分配或调用其它函数的 opcode 通过 helper 执行，由 VM 现有根集合保护存活对象。`BaselineFrame::stackData` 只是栈快照，不是新增的 GC 根；native 寄存器里的裸对象指针也不会自动被标记。

新增快路径若要持有 GC 对象并调用可能触发 GC 的 helper，必须先把对象保存在有效 VM 栈槽或 `temporaryRoots_` 中。仅把对象地址放在寄存器里不够。进入 helper 时真实 VM 栈必须已经同步，GC 才能看到正确的存活槽。

现有 helper 调用是后续 safepoint 设计可以利用的边界，尤其是对象/数组/闭包/类创建和函数调用；`Loop` 的回边 helper 也是未来中断检查的候选位置。目前没有独立 safepoint 表、寄存器根映射或从 native PC 恢复语言帧的协议，不能把这些候选位置当成已经实现的 deopt 点。

### Windows x64 数字局部变量

`GetLocal` 和 `SetLocal` 都先计算 `slotStart + slot`，检查编号加法溢出和栈边界，再访问 Value。越界时交给完整 helper 报错，不提前读取非法地址。

数字 `GetLocal` 仍需调用 `minijsBaselinePushNumberSlot()`。它通过 `stack_.emplace_back(0.0)` 在新栈槽直接构造 Number，刷新 `stackData` 和 `stackSize`。机器码随后重新计算局部槽和栈顶地址，用 `movsd` 复制 double 载荷。不跨 helper 保存裸栈指针，也不把 vector 的预留容量当作已构造 Value；栈槽构造失败时直接退出，不再调用完整 helper 重试。

数字 `SetLocal` 要求目标槽和栈顶都是 Number，直接用 `movsd` 写入目标载荷，不弹栈、不改变类型标签，也不需要同步栈深度。目标槽原先持有对象或字符串、或者源值不是数字时，完整 helper 执行正常 Value 赋值，负责资源生命周期和类型转换。

局部槽可能被 open upvalue 引用，原地修改仍会被闭包读到；当前调用帧的 `slotStart` 也保证普通函数和方法使用各自的槽编号。类型切换、捕获槽更新、方法 receiver 偏移和递归扩容均由回归测试覆盖。

### OP_POP 无复制丢弃

`pop()` 的语义是取出并返回栈顶 Value；算术、返回等操作需要该值。`OP_POP` 只需要丢弃栈顶，原先调用 `pop()` 会复制一个随后立即销毁的 Value。

普通 VM、decoded baseline 和 native runtime helper 的 `OP_POP` 现在统一使用 `discardTop()`：

```cpp
if (stack_.empty()) {
  throw RuntimeError("bytecode stack underflow");
}
stack_.pop_back();
```

`std::vector` 负责销毁最后一个 Value；字符串和 shared_ptr 等成员仍执行正常析构。GC 管理对象不会被直接 delete，移除的只是该栈槽中的引用，存活对象仍由其它根保护。空栈错误文本和 helper 的异常退出协议保持一致。

native `OP_POP` 仍调用完整 helper，因此这项优化适用于所有值类型，以及 ARM64 和 Windows x64 的 helper 路径。没有新增 Number guard 或直接修改 vector 布局；helper 成功后照常刷新栈地址和深度。

测试分别调用普通 VM、decoded executor 和 native entry，丢弃数字、字符串、数组、对象、布尔值和闭包临时值，再触发 GC 验证存活的捕获对象，同时检查普通 VM 和 native helper 的空栈错误。

### Windows x64 数字常量

编译 `OP_CONSTANT constantIndex` 时，verifier 已检查常量索引。Windows x64 后端直接查看常量类型；Number 使用 `memcpy` 获取 double 的 64 位原始编码并嵌入机器码，运行时无需再次访问常量池或检查类型。非数字继续调用完整 `minijsBaselinePushConstant()`，保留字符串的 GC 分配语义。

等价 C++ 操作如下：

```cpp
// 运行时通过 helper 构造完整 Value 并更新 stackData / stackSize。
stack_.emplace_back(0.0);
// helper 成功后重新定位栈顶，将编译时保存的数字位编码复制到 number_。
```

对应机器码的主要汇编操作为：

```asm
mov rcx, rbx                         ; 第一个参数是 BaselineFrame*
mov rax, pushNumberSlotHelper         ; 加载构造栈槽的 helper 地址
call rax
test eax, eax                        ; 构造失败则跳过写入并退出 native entry
jz done
; 从刷新后的 frame.stackData / stackSize 重新计算栈顶 Value*，放入 rcx
mov rdx, numberBits                  ; 装入 double 的原始 64 位编码
mov [rcx + numberOffset], rdx         ; 将原始位写入已构造的数字槽
mov eax, 1                           ; opcode 执行成功
done:
```

`numberBits` 是 double 的位编码，不是把整数转换成浮点数；因此负零、无穷大、非规格化数和 NaN payload 都能原样保留。机器码没有保存常量池对象地址，也没有跨 helper 保存栈指针。helper 失败时不再重试完整常量 helper，避免重复压栈。

该路径仍需要一次 C++ helper 来构造栈槽和处理 vector 扩容，并非完全无 helper 的压栈。构造已改用 `emplace_back(0.0)`：构造函数接收 double 参数，直接初始化 vector 的新元素，不创建临时 Value，也不通过 `push(Value)` 再移动一次。现有元素的扩容迁移仍由 vector 管理；helper 返回后仍必须重新读取栈地址。测试覆盖特殊数字逐位比较、连续 80 次压栈引发的扩容，以及数字与字符串、布尔值、null/undefined 混合的 JIT on/off 结果。

### Windows x64 布尔条件跳转

`OP_JUMP_IF_FALSE` 现在先检查栈非空且栈顶类型为 Boolean，直接读取布尔载荷作为 `0/1`。数字、字符串、对象等类型仍调用 `minijsBaselinePeekTruthy()`；空栈也交给该 helper 产生原有错误。ARM64 路径保持不变。

先按等价 C++ 理解控制流（下列字段访问是示意，不是公开 Value API）：

```cpp
if (frame.stackSize == 0 || top.type != Boolean) {
  state = minijsBaselinePeekTruthy(&frame); // 0 假、1 真、2 失败
} else {
  state = top.boolean_ ? 1 : 0;
}
if (state == 0) goto bytecodeTarget;
if (state != 1) goto epilogue;
// 为真时继续下一条指令。
```

对应汇编按操作顺序理解：

```asm
mov rax, [rbx + stackSize]           ; 读取栈深度
test rax, rax                       ; 检查是否为零
jz fallback                         ; 空栈交给 helper 报错
; 计算栈顶 Value*，放入 rcx
cmp dword ptr [rcx + typeOffset], Boolean
jne fallback                        ; 非 Boolean 使用完整 truthiness 语义
movzx eax, byte ptr [rcx + booleanOffset] ; 读取一字节，并将高位清零
jmp dispatch                        ; 跳过 helper
fallback:
mov rcx, rbx                        ; 第一个参数是 BaselineFrame*
mov rax, peekTruthyHelper
call rax                            ; 返回 0/1/2
dispatch:
; 0 跳到 bytecode 目标，1 继续，其它值退出。
```

`movzx` 不改变 Boolean 的值，只把一字节的 `0/1` 扩展为寄存器结果。快路径不修改 Value，不弹栈，也无需同步栈深度；后续 `Pop` 或短路表达式负责处理条件值。因此 `false && value` 保留 false，`true || value` 保留 true，不会执行被短路的一侧。

测试覆盖 decoded/native 的真假条件、非 Boolean fallback、JIT on/off 的逻辑运算结果与副作用、比较结果驱动的循环，以及空栈错误。NaN 的真假性沿用当前 MiniJS 的 `Value::isTruthy()`，没有借此修改语言语义。性能对照见 [数字循环性能报告](baseline-numeric-benchmark.md)，目前未证明稳定提速。

### Windows x64 布尔取反

`OP_NOT` 的 Boolean 快路径直接翻转栈顶的布尔载荷；非 Boolean 或空栈仍调用 `minijsBaselineNot()`。ARM64 保留完整 helper。类型 guard 在写入之前完成，失败时 helper 看到未修改的栈。

先看等价 C++（字段访问是示意）：

```cpp
if (frame.stackSize == 0 || top.type != Boolean) {
  success = minijsBaselineNot(&frame);
} else {
  top.boolean_ = !top.boolean_;
  success = true;
}
if (!success) goto epilogue;
```

对应汇编的关键部分如下，前面的空栈检查和栈顶寻址与布尔条件跳转相同：

```asm
cmp dword ptr [rcx + typeOffset], Boolean
jne fallback                         ; 非 Boolean 使用完整 helper
xor byte ptr [rcx + booleanOffset], 1 ; 0 变为 1，1 变为 0
mov eax, 1                           ; 表示 opcode 成功，不是取反结果
jmp done
fallback:
mov rcx, rbx                         ; 第一个参数是 BaselineFrame*
mov rax, notHelper
call rax                             ; 返回操作成功状态
done:
; 失败时跳到统一 epilogue，成功时执行下一条 opcode。
```

Boolean 载荷为 0 或 1，异或 1 实现 `!`。类型标签和其它成员不变，栈深度不变，不调用分配 helper，也不需要同步栈快照。`!true` 虽然产生 false，操作仍然成功，因此这里不能复用 `JumpIfFalse` 的三态真假结果。

字符串、数字、对象、数组、实例和函数继续由完整 helper 按 `isTruthy()` 计算结果并管理 Value 生命周期。测试分别验证 decoded/native 的真假值和类型 fallback，以及 JIT on/off 的连续取反、条件分支、循环、函数调用副作用和空栈错误。

`examples/baseline_boolean_benchmark.js` 专门覆盖一次取反和连续两次取反。CLI smoke 测试检查 baseline 入口计数；Release 前后对照记录在 [性能报告](baseline-numeric-benchmark.md)。该样例与数字循环不同，二者耗时不能直接交叉比较。

### Windows x64 相邻局部值与数字常量压栈

以下相邻 bytecode 在 Windows x64 native 编译时可以合并：

```text
OP_GET_LOCAL slot
OP_CONSTANT numberIndex
```

原来数字局部值和数字常量各调用一次 `PushNumberSlot`。现在一次 `minijsBaselineGetLocalAndPushNumberSlot(frame, slot)` 完成两个槽的正常构造，随后机器码把常量位编码写入栈顶。没有新增 bytecode，decoded executor 和 ARM64 仍按两条指令执行。

helper 的等价操作如下：

```cpp
// 检查局部槽有效；先取出局部值，再进行可能扩容的压栈。
// 数字仅复制 double，非数字保留完整 Value。
stack_.emplace_back(localNumber); // 非数字使用完整 Value 副本
stack_.emplace_back(0.0);
refreshBaselineFrameStack(frame);
// helper 成功后，native 重新定位栈顶并写入常量的原始 64 位编码。
```

两次 Value 构造都由 C++ vector 管理，并不是把 `reserve()` 的容量直接当作有效槽。helper 不分配 GC 对象；非数字局部值的复制保留引用身份。若第二次构造失败，第一次压栈可能已完成，符合逐条执行的副作用顺序；错误直接退出，不从头重试。

codegen 先收集所有 `Jump`、`JumpIfFalse` 和 `Loop` 目标。如果 `Constant` 是独立跳转入口，禁止合并，避免跳转时额外执行前面的 `GetLocal`。非数字常量也不合并。两个 decoded 指令和 bytecode 编号均保留；被合并的两个 native offset 映射指向同一单元，不能用第二条的映射作为独立执行或 deopt 入口。目前没有执行中 deopt，因此该映射只用于定位合并单元。

测试覆盖数字、负零、NaN、字符串、布尔值、null、数组，语言内部对象身份、递归扩容、直接跳转到第二条，以及非法局部槽错误。采样证明调用次数减少，但关闭采样的三组性能对照未证明稳定提速，具体数据见 [性能报告](baseline-numeric-benchmark.md)。

### 后续 Deopt 需要补什么

真正的 deopt 要在 native 执行中恢复普通 VM，并从正确的 bytecode 位置继续。除了现有 `bytecodeOffsetToNativeOffset` 映射，还需要记录恢复点对应的 bytecode offset、操作发生前还是发生后、有效栈深度、局部变量与操作数的位置，以及嵌套调用帧状态。

当前 offset 映射用于分支回填和定位指令起点，不包含上述恢复信息。现有类型 guard 直接调用当前 opcode 的 helper，因此暂时无需恢复整个 VM 帧。未来只有确定恢复状态和副作用边界后，才可以增加执行中切回 VM 的机制。

## 下一步

1. 对现有数字快路径集中验证 guard 命中、类型 fallback、零除数、NaN 和嵌套 helper 调用后的栈快照刷新。
2. 数字局部变量、数字常量、Boolean 条件跳转和取反已有快路径，`OP_POP` 已消除丢弃时的 Value 副本，数字槽已改为原地构造；[性能报告](baseline-numeric-benchmark.md) 记录 Release 前后对照。后续先测量剩余 helper 成本，再选择下一条值得内联的 opcode。
3. 引入执行中恢复需求时，再增加 safepoint 元数据和 deopt 帧恢复协议。
