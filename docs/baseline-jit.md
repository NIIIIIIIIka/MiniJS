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
- [x] Baseline executor 已复用 VM runtime helper 支持 global、array/index、object/property、upvalue 和 current-closure opcode。
- [x] `BaselineFrame` 和 `BaselineEntry` 已定义，第一批 `extern "C"` runtime helper 已通过稳定入口帧访问 VM。
- [x] `BaselineCompiler` 已能为 `Constant`、`GetLocal`、算术 opcode 和 `Return` 生成 ARM64 和 Windows x64 stub，并写入 executable memory；Windows x64 的 `Negate`、`Add`、`Sub`、`Mul`、`Div` 和 `Mod` 已有 number fast path / guarded number helper。
- [x] VM baseline dispatch 已能在支持 native backend 且存在 native `BaselineEntry` 时优先进入机器码；没有 native entry 时继续使用 decoded executor。

还没有完成：

- [ ] `Call`、`MethodCall`、`SuperCall` 的嵌套调用 frame 协议。
- [ ] `Closure`、`Class`、`Method`、`StaticMethod`、`Inherit` 的 baseline 执行路径。
- [ ] safepoint、deopt 或 fallback 协议。
- [ ] 平台相关 codegen 后端。

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

native 入口接收一个 `BaselineFrame*`。frame 保存 VM、当前闭包、局部槽起点、返回槽和执行结果状态。stub 不直接访问 `std::vector<Value>` 或 GC 对象布局，而是调用 `extern "C"` helper；helper 再转发到 `VM::baselineRuntime*()`，完成压栈、局部变量访问、算术和返回等操作。

helper 用 `bool` 表示成功或失败，并通过 `BaselineFrame::failed`、`completed` 把状态带回 C++。这样异常和复杂 C++ 对象都不会跨越 native ABI 边界。

相关代码：

- [include/minijs/baseline_frame.h](../include/minijs/baseline_frame.h)：`BaselineFrame`、`BaselineEntry`
- [include/minijs/baseline_runtime.h](../include/minijs/baseline_runtime.h)、[src/baseline_runtime.cpp](../src/baseline_runtime.cpp)：稳定的 C ABI helper
- [include/minijs/vm.h](../include/minijs/vm.h)、[src/vm.cpp](../src/vm.cpp)：`baselineRuntimePushConstant()`、`baselineRuntimeGetLocal()`、算术 helper、`baselineRuntimeReturn()` 等 VM 实现

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

机器码需要读写局部变量、压栈、算术或返回时，必须调用 Runtime ABI helper，例如：

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

`compileBaseline()` 会先尝试第一版 native `BaselineCompiler`。如果函数只包含 `Constant`、`GetLocal`、算术 opcode 和 `Return`，会在当前平台生成 native stub、`bytecodeOffsetToNativeOffset` 映射和 `BaselineEntry`。如果 native compiler 遇到暂不支持的 opcode 或当前平台没有 native backend，会回退到现有 decoded baseline compiler；这样已经支持的 runtime helper opcode 仍能通过 decoded executor 验证语义。

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
Value::numberTypeTag()
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
| `OP_CONSTANT` | `constantIndex` | `minijsBaselinePushConstant(frame, constantIndex)` | 从常量池取值并压入 VM 栈 |
| `OP_GET_LOCAL` | `slot` | `minijsBaselineGetLocal(frame, slot)` | 读取 `slotStart + slot` 的局部槽并压栈 |
| `OP_ADD` | 无 | Windows x64 number fast path，否则 `minijsBaselineAdd(frame)` | 两个操作数都是数字时直接执行 double 加法并同步栈高度；否则回退 helper 保留字符串拼接语义 |
| `OP_SUB` | 无 | Windows x64 number fast path，否则 `minijsBaselineSub(frame)` | 两个操作数都是数字时直接执行 double 减法并同步栈高度；否则回退 helper 保留 VM 语义 |
| `OP_MUL` | 无 | Windows x64 number fast path，否则 `minijsBaselineMul(frame)` | 两个操作数都是数字时直接执行 double 乘法并同步栈高度；否则回退 helper 保留 VM 语义 |
| `OP_DIV` | 无 | Windows x64 number fast path，否则 `minijsBaselineDiv(frame)` | 两个操作数都是数字且除数非 0 时直接执行 double 除法并同步栈高度；否则回退 helper 保留 VM 语义 |
| `OP_MOD` | 无 | Windows x64 guarded number helper，否则 `minijsBaselineMod(frame)` | 两个操作数都是数字且除数非 0 时调用 `minijsBaselineModNumber(frame)`；否则回退 helper 保留 VM 语义 |
| `OP_NEGATE` | 无 | Windows x64 number fast path，否则 `minijsBaselineNegate(frame)` | 栈顶是数字时直接翻转符号位；否则回退 helper 保留 VM 语义 |
| `OP_EQUAL` | 无 | `minijsBaselineEqual(frame)` | 弹出两个值，通过 `Value::equals()` 比较后压入布尔结果 |
| `OP_GREATER` | 无 | `minijsBaselineGreater(frame)` | 弹出两个值，执行数字大于比较后压入布尔结果 |
| `OP_LESS` | 无 | `minijsBaselineLess(frame)` | 弹出两个值，执行数字小于比较后压入布尔结果 |
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

`BaselineCode::bytecodeOffsetToNativeOffset` 会记录每条 bytecode 指令对应的机器码起点。当前它主要用于测试和调试，后续 safepoint、deopt 或源码级 profiling 也可以从这里建立 bytecode offset 和 native offset 的关系。

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

- 纯表达式和局部变量：`Constant`、`Add`、`Sub`、`Mul`、`Div`、`Mod`、`Negate`、`Equal`、`Greater`、`Less`、`Not`、`GetLocal`、`SetLocal`、`Pop`、`Return`
- 控制流：`JumpIfFalse`、`Jump`、`Loop`

第六阶段继续支持不引入新调用帧的 runtime helper opcode：

- 全局变量：`DefineGlobal`、`GetGlobal`、`SetGlobal`
- 数组与下标：`Array`、`GetIndex`、`SetIndex`
- 对象与属性 IC：`Object`、`GetProperty`、`SetProperty`
- 闭包访问：`GetUpvalue`、`SetUpvalue`、`CloseUpvalue`、`GetCurrentClosure`

函数调用、方法调用、类定义、闭包创建、继承和 `super` 暂时编译失败并回退 VM。后续扩展时优先直接为现有 `Opcode` 增加执行路径或 runtime helper 路径，不新增平行 opcode。

第七阶段 native compiler 的第一版平台 backend switch 只覆盖：

- `Constant`
- `GetLocal`
- `Add`、`Sub`、`Mul`、`Div`、`Mod`、`Negate`
- `Return`

这些指令不会内联访问 `VM::stack_` 或 `std::vector<Value>` 内部地址，而是统一通过 Runtime ABI helper 完成语义。每条 helper 调用后的 `false` 返回值都会被修补成跳转到 epilogue，`Return` 成功后也跳到 epilogue。

## 执行模型

Baseline frame 复用 VM 的调用约定：

- 参数仍位于 VM `stack_` 的 callee/receiver 后方。
- `returnSlot` 和 `slotStart` 与 `CallFrame` 保持一致。
- 局部变量仍使用 bytecode slot 编号。
- 返回值仍写回调用方期待的位置。
- GC roots 仍来自 `stack_`、`frames_`、`openUpvalues_` 和 `temporaryRoots_`。

当前 baseline dispatch 已接入 `callBytecodeClosure()` 的函数入口。当前平台支持 native backend 且 `BaselineCode::entry != nullptr` 时，VM 会优先进入 native `BaselineEntry`；否则继续进入 decoded baseline executor。测试入口仍保留，用于直接验证 code object 和 runtime helper ABI。

非调用型 runtime opcode 通过 VM helper 与解释器共享语义，例如 global 读写、数组创建、对象属性读写和 upvalue 读写。baseline 只负责从 `DecodedInstruction::operands` 取操作数，不直接读取或推进 `frame.ip`。

## 下一步

1. 设计 `Call`、`MethodCall`、`SuperCall` 的嵌套调用 frame 协议。
2. 为 `Closure` 和 class-family opcode 增加 GC 安全的 baseline helper。
3. 扩展 benchmark，用于对比解释执行、IC 与 baseline dispatch 路径。
4. 设计 safepoint、deopt 和 fallback 边界。
5. 逐步把更多 opcode 从 decoded executor 下沉到 native stub 或更细的 runtime helper。
