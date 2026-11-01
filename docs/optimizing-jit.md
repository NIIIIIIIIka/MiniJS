# MiniJS Optimize JIT 设计

状态日期：2026-10-14。开发分支：`feature/optimizing-jit`。

本文描述 MiniJS Optimize JIT 的实施设计。当前分支已经实现 CFG、栈高度分析、首版 Stack-to-SSA、IR verifier 和 IR Interpreter；Optimize tier 尚未接入 VM 调度，也没有生成 optimized native code。

## 1. 定位与第一版目标

MiniJS 的执行层次保持为：

```text
AST Interpreter
  -> Bytecode VM
  -> Baseline JIT
  -> Optimize JIT
```

Baseline JIT 负责快速建立执行入口，并保留 runtime helper 作为完整语义边界。Optimize JIT 读取 bytecode 和运行时反馈，把稳定的动态行为转换成带 guard 的类型专门化代码。

第一版只打通一条可以证明正确的优化链路：

```text
热点纯数字函数
  -> CFG
  -> Stack-to-SSA
  -> 类型推断
  -> 函数入口 guard
  -> 数字专门化
  -> optimized native entry
  -> guard 失败时安全回 Baseline
```

## 2. 当前基础

### 2.1 已有运行时能力

- Bytecode verifier 和 `DecodedInstruction`。
- 函数调用、循环回边热度和 Baseline tiering。
- Windows x64 Baseline native entry 与 decoded fallback。
- `BaselineFrame`、Runtime ABI 和共享 `VM::stack_`。
- Hidden Class、属性/方法 Inline Cache 和 `FeedbackSlot`。
- Mark-Sweep GC 及 VM 栈、调用帧、globals 等根集合。

Baseline 已进入功能冻结。Optimize JIT 不通过继续堆叠 Baseline opcode 模板实现，而是建立独立 IR、反馈、guard 和代码对象。

### 2.2 当前分支已经实现

代码位置：

- `include/minijs/optimizer_ir.h`
- `src/optimizer_ir.cpp`
- `tests/test_optimizer_ir.cpp`

已经完成：

1. 从已验证 bytecode 划分 basic block。
2. 建立 predecessor/successor CFG 边。
3. 验证 block 边界、终结指令和完整 bytecode 分区。
4. 使用 worklist 传播栈高度。
5. 拒绝栈下溢、非法 local slot 和合流点高度冲突。
6. 将参数、局部槽和操作数栈统一转换成 SSA value。
7. 为分支合流和循环携带值生成 Phi。
8. 将隐式 fallthrough 转换成显式 IR `Jump`。
9. 验证 value 定义、输入编号、Phi 位置和输入数量。
10. 使用 IR Interpreter 执行算术、比较、分支和循环。
11. 使用真实 Compiler 产出的函数对 VM 与 IR 做差分测试。

当前 IR opcode：

```text
Parameter  Constant  Phi
Add        Sub       Mul       Div       Mod
Negate     Equal     Greater   Less      Not
Branch     Jump      Return
```

当前 IR 是正确性原型，仍保留通用 `Value` 语义，没有类型反馈、guard、deopt metadata 或机器码。

## 3. 第一版范围

第一版只优化可证明为“函数内纯数字计算”的函数。

允许：

- number 参数、number/boolean 常量。
- 局部变量。
- `Add/Sub/Mul/Div/Mod/Negate`。
- 数字比较和布尔分支。
- `if`、`while`、`for` 形成的 CFG。
- 函数入口执行。
- 函数内部 Phi 和循环携带值。
- 返回 number 或 boolean。

拒绝并继续使用 Baseline：

- globals 读写。
- 对象、数组和属性操作。
- 普通调用、方法调用和 `super`。
- closure、upvalue、class 和继承。
- 字符串 `+`。
- native/builtin 调用。
- 任何无法建立安全类型或恢复边界的 opcode。

第一版不做 OSR、跨函数内联、任意位置 deopt、GC 对象寄存器根、shape/class guard，以及 CSE、LICM、逃逸分析等高级优化。

这个边界使入口 guard 失败时可以安全地从函数入口执行 Baseline。后续一旦允许 globals、对象写入或调用，就不能在执行过副作用后无条件重跑函数入口。

## 4. 编译管线

```text
BytecodeFunction
  -> verifyBytecode
  -> DecodedInstruction
  -> OptimizerControlFlowGraph
  -> stack-depth dataflow
  -> OptimizerIrFunction（SSA）
  -> type inference
  -> eligibility analysis
  -> guard insertion
  -> Phi simplification / constant folding / DCE
  -> lowered numeric IR
  -> optimized code generation
  -> OptimizedCode
```

任一阶段失败都返回结构化原因，函数继续使用已有 Baseline code。Optimize 编译失败不能把函数的 Baseline 状态标记为 `Failed`。

## 5. CFG、栈与 SSA

### 5.1 Basic block

以下位置是 leader：

- bytecode offset 0。
- 所有跳转目标。
- `JumpIfFalse`、`Jump`、`Loop`、`Return` 后仍存在的下一条指令。

IR 中所有控制流都使用 `Branch` 或 `Jump`。即使 bytecode 原本依赖物理顺序 fallthrough，也生成显式 `Jump`。

### 5.2 统一帧槽

Bytecode VM 把函数参数、局部变量和临时操作数放在同一条 `VM::stack_` 上，因此 Optimizer 对整个帧区间传播统一槽向量，而不是维护互相独立的 locals 和 operand stack。

`initialDepth`：普通函数为参数数量；实例方法为 receiver 加参数数量。每个合流点必须得到相同栈高度，否则拒绝优化。

### 5.3 SSA 规则

- `Parameter(slot)` 定义入口槽。
- `Constant(index)` 引用原 `Chunk` 常量池。
- `GetLocal(slot)` 复用当前槽的 SSA value。
- `SetLocal(slot)` 将栈顶 SSA value 绑定到目标槽。
- 算术和比较消费输入并产生新 value。
- block exit 保存完整槽向量，供后继 Phi 使用。

首版允许冗余一输入 Phi，先保证正确性，再由独立 pass 简化。循环携带值形如：

```text
v0 = Parameter(0)
v1 = Phi(v0, vNext)
...
vNext = Add(v1, 1)
Jump(loopHeader)
```

## 6. 类型系统

下一阶段增加最小类型格：

```text
Bottom
  -> Number / Boolean / Null / Undefined / String / Object
  -> Unknown
```

它表示数据流抽象类型，不是类继承关系；不同具体类型合流为 `Unknown`。

首版规则：

| IR | 输入 | 输出 |
| --- | --- | --- |
| `Parameter` | 入口 profile | profile 类型或 Unknown |
| `Constant` | 常量池 | 常量的精确类型 |
| `Phi` | 所有输入 | 类型 join |
| `Sub/Mul/Div/Mod/Negate` | Number | Number |
| `Add` | Number + Number | Number |
| `Less/Greater` | Number + Number | Boolean |
| `Equal` | 已知 primitive | Boolean |
| `Not` | 任意已知值 | Boolean |

推断使用 worklist 迭代到不动点，以处理循环 Phi。若某个数值运算输入最终为 `Unknown`，且入口 guard 不能证明它来自 number 参数，则拒绝优化。

## 7. 运行时反馈

现有 `FeedbackSlot` 只覆盖属性和方法，不能证明算术类型。建议在 Baseline 生命周期旁增加独立反馈：

```cpp
enum class ObservedType : std::uint8_t {
  Uninitialized,
  Number,
  Boolean,
  Other,
  Polymorphic,
};

struct OptimizeFeedback {
  std::vector<ObservedType> parameterTypes;
  ObservedType returnType = ObservedType::Uninitialized;
  std::uint32_t optimizeAttempts = 0;
  std::uint32_t optimizedEntries = 0;
  std::uint32_t guardFailures = 0;
  std::uint32_t deopts = 0;
};
```

函数入口记录参数类型，返回时可记录返回类型。状态只单向演化：

```text
Uninitialized -> Number/Boolean/Other -> Polymorphic
```

第一版只为所有相关参数稳定为 `Number` 的函数生成 optimized code；`Polymorphic` 继续 Baseline，不生成多版本代码。

## 8. Guard 与 Number lowering

optimized entry 首先执行入口 guard：

```text
GuardType(arg0, Number)
GuardType(arg1, Number)
```

guard 通过后，参数解包成 double，后续运算不再重复检查 `Value` tag。guard 失败发生在任何用户可观察副作用之前，可直接进入 Baseline。

类型确认后，将通用节点降低为：

```text
UnboxNumber
AddNumber  SubNumber  MulNumber  DivNumber  ModNumber
NegateNumber  CompareNumber
BoxNumber
```

只有返回或 runtime 边界需要重新构造 `Value`。

当前实现使用独立的 `OptimizerLoweredFunction`，并为每个 value 记录
`Tagged`、`Number` 或 `Boolean` 表示。参数先以 `Tagged` 进入，经过
`GuardNumber` 和 `UnboxNumber` 后才参与数值运算；比较、真假判断和 Phi
分别降低为带表示的专用节点，返回值通过 `BoxNumber`/`BoxBoolean` 回到
ABI 所需的 `Value`。lowered verifier 检查唯一值定义、CFG 反向边、Phi
输入数、终结指令位置，以及每个 opcode 的输入/输出表示约束。独立的
lowered IR interpreter 用于在 native codegen 之前验证循环、guard、装箱和
错误路径的语义。

`Div` 和 `Mod` 必须保留零除数检查。优先调用保留原错误语义的 helper，确保仍产生 `division by zero` 和 `modulo by zero`，而不是静默生成浮点无穷或 NaN。

## 9. 优化 pass

第一版顺序：

1. Type inference。
2. Eligibility analysis。
3. Entry guard insertion。
4. Redundant Phi elimination。
5. Constant folding。
6. Constant branch folding。
7. Dead code elimination。
8. Number lowering。
9. Lowered IR verifier。

每个 pass 前后都能运行 verifier。第一版不加入需要复杂别名或副作用分析的 pass。

## 10. Tiering 与代码对象

Baseline 与 Optimize 状态分开保存：

```cpp
enum class OptimizeState : std::uint8_t {
  Cold,
  Scheduled,
  Compiled,
  Disabled,
  Failed,
};

struct OptimizedCode {
  OptimizerIrFunction ir;
  OptimizerLoweredFunction lowered;
  std::shared_ptr<ExecutableMemory> executableMemory;
  OptimizedEntry entry = nullptr;
};
```

`JitFeedback::state` 继续描述 Baseline 生命周期；新增 `OptimizeState` 描述优化 tier。

当前实现已经把两套状态完全分开：`OptimizeFeedback` 保存参数 profile、
`compileAttempts`、入口/guard 计数、`OptimizeState`、独立编译错误和
`OptimizedCode`。Optimize 使用 `optimizeThreshold`，按 Baseline 实际入口次数
触发，不复用 Baseline 的 call/backedge 阈值。同步编译成功后保存高层优化 IR
和 lowered IR；native entry、可执行内存、guard failure 分派与入口 safepoint map
均已接入。Optimize 编译失败只设置 Optimize 的 `Failed`，不会修改
`JitState::Compiled` 或丢弃 `BaselineCode`。

Optimize 调度条件：

- Baseline 已成功编译。
- `baselineEntryCount` 达到独立 Optimize 阈值。
- 参数 profile 稳定为 Number。
- bytecode、CFG、栈和 IR verifier 全部通过。
- 函数通过纯数字 eligibility analysis。
- guard failure/deopt 未超过禁用阈值。

dispatch 优先尝试 optimized entry；入口 guard 失败则增加计数并进入 Baseline。连续失败达到阈值后设为 `Disabled`。

## 11. Native codegen

第一版只实现 Windows x64，与当前已经验收的平台保持一致。可以复用 `ExecutableMemory`、Baseline 已验证的调用约定处理、`Value` 布局知识和 helper 异常隔离规则，但不直接复用 `BaselineCode`。

当前 Windows x64 首版已经生成独立 native entry：入口遵循 Windows x64 ABI，
分配 32 字节 shadow space、保存 callee-saved frame 寄存器，通过嵌入的绝对地址
调用 `noexcept` lowered-runtime 边界，并由 `ExecutableMemory` 完成 RW 到 RX 的
内存切换和生命周期管理。VM 在 Baseline 之前优先进入该入口，成功时写回返回值
并增加 `optimizedEntryCount`；guard 失败则保留原调用栈并进入 Baseline。

这一版采用 helper-call stub 作为最小后端，先验收 native ABI、入口分派、代码内存
和异常边界。lowered IR 的算术仍由 correctness oracle 执行；后续性能阶段再把
`AddNumber`、`PhiNumber`、branch 等逐步替换成直接 SSE2 指令和 spill slot。

入口 ABI 应使用独立 `OptimizedFrame`，或明确版本化扩展现有 frame。optimized code 不能依赖 `std::vector<Value>` 内部布局；调用 helper 后必须重新读取可能失效的栈快照。

首版寄存器分配采用简单 linear scan：

- 计算 SSA live interval。
- double 优先分配 XMM 寄存器。
- 溢出值进入 native spill slot。
- block 边执行 parallel move，满足 Phi 分配。
- 返回前 box 成 `Value` 并按 VM 约定写回。

## 12. Deopt 策略

### 12.1 第一版：整函数安全回退

第一版不宣称支持任意位置 deopt。只接受纯数字、无外部副作用函数：

- 入口 guard 失败：进入 Baseline。
- native 编译失败：保留 Baseline。
- 预检查失败：进入 Baseline。
- 需要复杂 runtime 语义：eligibility analysis 直接拒绝。

由于没有 globals、对象写入、调用或 I/O，从函数入口回退不会重复外部副作用。

当前实现只在 synthetic preheader 中执行参数 `GuardNumber`。native entry 返回
`guardFailed` 时尚未执行用户可观察操作，VM 不弹出参数或创建 Optimized
`CallFrame`，而是沿用原栈直接进入已经编译的 Baseline。每次失败增加饱和的
`guardFailures`；达到独立的 `optimizeGuardFailureThreshold`（默认 3）后，
Optimize 状态变为 `Disabled`。此后分派跳过 optimized entry，但继续保留
`OptimizedCode` 用于诊断，且不会修改 Baseline 的 `JitState::Compiled`、代码对象
或入口计数。阈值可单独配置，成功的 optimized entry 不增加失败计数。

### 12.2 第二版：精确 deopt

允许对象访问或中途 guard 前，必须记录：

```cpp
struct DeoptState {
  std::size_t bytecodeOffset;
  DeoptPhase phase;  // 当前操作之前或之后
  std::vector<ValueLocation> frameSlots;
  std::size_t stackDepth;
};
```

恢复时重建真实 `VM::stack_`，设置正确 `CallFrame::ip`，再进入 VM 或经过认证的 Baseline 恢复点。现有 `bytecodeOffsetToNativeOffset` 只是定位映射，不包含恢复状态，不能直接视为 deopt metadata。

当前 metadata 基础已经实现。每个 lowered guard 获得稳定 `deoptId`，对应的
`DeoptState` 记录 bytecode offset、`Before/After` phase、stack depth，以及每个
frame slot 的恢复 recipe（参数、数字常量或布尔常量）。编译阶段 verifier 要求
guard 与 state 一一对应、offset 一致、ID 唯一且连续、槽数等于 stack depth，
并检查参数索引边界。guard failure 通过 `OptimizedFrame` 把 `deoptId` 带回 VM；
VM 先按 recipe 物化槽位并校验恢复点，再进入 Baseline。

Number guard 的恢复点是 offset 0；monomorphic property guard 使用实际属性
bytecode offset，phase 同样为 `Before`。两者都在任何用户可观察副作用之前完成，
因此当前仍可按恢复 recipe 重建入口参数并安全重跑 Baseline。数据结构不把恢复
来源绑定到物理寄存器；未来直接 SSE2 codegen 加入更深的中途 guard 时，可扩展
location kind 为 register/spill，并复用相同 ID、验证和物化通路。

## 13. GC 与 safepoint

纯数字第一版不把 GC 对象保存在 optimized 寄存器或 spill slot，但仍需遵守：

- 进入 helper 前同步 VM 可见状态。
- helper 返回后重新读取栈快照。
- `OptimizedCode` 生命周期长于 active optimized frame。

未来加入对象优化时：

- 每个可能分配或触发 GC 的调用点都是 safepoint。
- safepoint 记录 live GC reference 的位置。
- code object 缓存的 `ObjShape*`、`ObjClass*`、`ObjClosure*` 必须参与标记或由明确的其它根保活。
- 已发生副作用后的 shape/class guard 失败必须精确 deopt。

当前实现为每个 `OptimizedCode` 生成并验证入口 safepoint。map 使用稳定的 safepoint
ID，精确列出仍可能保存 GC 引用的参数索引：纯 Number 路径为空，property/method
路径包含 profile 为 Object 的参数。VM 调用 native entry 前把 `OptimizedFrame` 注册到
活动 optimized frame 集合，并设置入口 safepoint ID；`markRoots()` 会通过 map 标记这些
参数，退出 native entry 时无论正常返回还是异常都会注销 frame。direct method call 在
离开 native entry 后才重排 VM 栈并进入被调方法，此时 receiver 和实参重新成为普通 VM
栈根，不依赖已经失效的 `OptimizedFrame::arguments` 指针。

这一阶段只需要参数位置，因为现有 helper-call backend 不把新的 GC 引用长期保存在机器
寄存器或 spill slot。以后加入直接机器码对象操作、可分配 helper 或内联后，需扩展 location
kind，并为每个调用点/回边生成独立 map；不得把入口 map 误用到任意程序点。

测试构建提供 safepoint stress hook：进入 native helper 前先把参数复制到独立 spill 区并清空
对应 VM 栈槽，然后在 OptimizedFrame 仍注册时强制执行 GC。临时对象此时不在 global、普通
VM 栈或 temporary root 中，只能由 safepoint map 找到；GC 返回后继续执行属性读取并校验
结果。该 hook 默认关闭，不影响正常执行路径。

## 14. 后续对象与方法优化

完成精确 deopt 后再使用现有 Inline Cache。

Monomorphic property read：

```text
GuardShape(object, cachedShape)
value = LoadSlot(object, cachedSlot)
```

Monomorphic property write：

```text
GuardShape(object, cachedShape)
StoreSlot(object, cachedSlot, value)
```

仅优化已有属性；新增属性、dictionary mode 和 missing property 走 runtime。

Method call：

```text
GuardClass(receiver, cachedClass)
CallDirect(cachedClosure)
```

第一阶段只做 direct call，不做内联；`super` 继续使用 runtime helper。

当前 method direct-call 首版只识别
`return receiver.method(arg0, ...)` 这种薄包装函数，并要求 receiver 与所有实参按参数
顺序原样转发。编译读取 `MethodCall` 的 monomorphic feedback，缓存唯一的
`(ObjClass*, ObjClosure*)`；optimized entry 先验证 receiver 是该 class 的实例，再由 VM
同步调用缓存 closure。class guard 失败携带调用 bytecode offset 对应的 deopt ID，从调用前
状态回到 Baseline，因而不会重复方法副作用。静态方法、`super`、多态/超多态反馈、参数
重排、调用前计算和方法内联暂不进入该路径。缓存的 class 与 closure 已加入 GC 标记。

当前 property 首版支持两种有界形态：`return object.name` 和
`return object.name = value`。编译要求 receiver 参数 profile 稳定为 Object，属性
feedback 严格为 monomorphic，并缓存唯一 `(ObjShape*, slot)`。optimized runtime
拒绝 dictionary object、shape mismatch 和越界 slot；load 直接读取槽，store 在全部
guard 通过后才写槽并返回赋值。shape guard 失败携带属性 bytecode offset 对应的
deopt ID 回 Baseline。新增属性、实例字段、多态/超多态 IC 和复合属性表达式仍由
eligibility 边界拒绝。`OptimizedCode` 缓存的 shape 已加入 `ObjFunction` GC 标记。

## 15. 测试与验收

当前基础测试覆盖：CFG 直线/分支/循环、非法 bytecode、栈错误、unsupported opcode、SSA 关系、loop-carried Phi，以及 VM/IR 算术和循环差分。

当前 Optimize 验收还覆盖：Debug 与 Release 全套 CTest、解释器/Baseline/Optimize
循环结果一致性、除零错误文本一致性、JIT 关闭时 Optimize 状态不推进、Baseline
和 Optimize 精确入口计数、native entry 的 RX 内存存在性、guard failure 回退及达到
阈值后的自动禁用。文本和 JSON benchmark 统计均输出 Optimize 状态数量、编译尝试、
实际入口和 guard failure，因此不能把“成功生成代码对象”误报为“已执行优化代码”。

类型阶段必须验证：

- 循环 Phi 类型能迭代到稳定结果。
- Number 与 Boolean 合流得到 Unknown。
- 非 number `Add` 不进入 number lowering。
- unsupported 类型不会污染 Baseline 状态。
- 每个 pass 前后 verifier 通过。

第一版 native Optimize 验收：

- 普通构建和独立 Release 全套 CTest 通过。
- JIT 关闭时行为不变。
- Optimize 关闭时仍使用现有 Baseline。
- 数字算术和循环与 VM/Baseline 结果一致。
- number guard 命中时确实进入 optimized entry。
- 非 number guard 失败时进入 Baseline。
- 除零、模零和类型错误文本一致。
- 编译失败和 unsupported opcode 安全回 Baseline。
- guard failure 达阈值后禁用 optimized entry。
- 统计区分 baseline entry、optimized entry、guard failure 和 deopt。

不能仅以“生成了机器码”作为通过标准，必须证明机器码实际进入。

### Optimize 专项 benchmark

`--benchmark-compare-optimize` 在两侧都启用 Baseline，只切换 Optimize tier，输出
`baseline_only`、`with_optimize`、两侧完整 JIT 统计和净 speedup。四个固定 workload
分别覆盖数字函数调用、monomorphic property getter、monomorphic method direct call，
以及只调用一次的长热循环 OSR 候选。CTest 同时断言前三类确实产生 optimized entry，
OSR 候选保持 `attempts=0 entries=0`，避免把未进入 Optimize 的计时误当优化结果。

2026-10-18 在 Windows x64 Release 构建上以 2 次预热、5 次采样中位数测得：

| workload | Baseline only | With Optimize | speedup | optimized entries |
| --- | ---: | ---: | ---: | ---: |
| numeric call | 43,615 us | 74,782 us | 0.58x | 48,999 |
| property getter | 63,795 us | 44,875 us | 1.42x | 48,999 |
| method direct call | 43,888 us | 51,731 us | 0.85x | 48,999 |
| single hot loop | 246,047 us | 242,540 us | 1.01x | 0 |

这些数字只用于当前机器上的方向判断，不作为跨机器性能承诺。结论是 property 特化已有
收益，但通用数字 lowered IR 仍由 C++ helper 解释，helper、装箱和临时 vector 成本使它
明显慢于 Baseline；method wrapper 也尚未抵消分派和同步调用成本。单次长循环完全无法达到
按函数入口计数的 Optimize 阈值，证明 OSR 存在覆盖缺口。不过在 helper backend 上先做
OSR 只会让更多循环进入当前较慢的执行器。因此下一优先级确定为纯数字 lowered IR 的直接
x64 codegen；完成并重新跑同一套 benchmark 后，再决定 loop-header OSR。函数内联继续
推迟到多位置 deopt、调用点 safepoint 和直接 codegen 稳定之后。

## 16. 实施顺序

1. `[x]` CFG 构建和结构验证。
2. `[x]` 栈高度数据流分析。
3. `[x]` Stack-to-SSA、Phi 和 IR verifier。
4. `[x]` IR Interpreter 与 VM 差分测试。
5. `[x]` 最小类型格与不动点类型推断。
6. `[x]` 纯数字 eligibility analysis。
7. `[x]` 参数类型 profile 与入口 guard。
8. `[x]` Phi 简化、常量折叠和 DCE。
9. `[x]` Number lowered IR、verifier 与执行 oracle。
10. `[x]` `OptimizeState`、`OptimizedCode` 和独立阈值。
11. `[x]` Windows x64 optimized native entry（首版 helper-call backend）。
12. `[x]` guard failure 到 Baseline 的整函数安全回退与自动禁用。
13. `[x]` Release 回归、入口计数和一致性验收。
14. `[x]` 精确 deopt metadata（当前入口 guard 范围）。
15. `[x]` monomorphic property read/write（直接 getter/setter 首版）。
16. `[x]` monomorphic instance method direct call（薄包装函数首版）。
17. `[x]` safepoint GC map（native entry 参数根首版）。
18. `[x]` OSR 和函数内联重新评估（benchmark 结论：先直接 x64 codegen）。
19. `[ ]` 纯数字 lowered IR 直接 x64 codegen。
20. `[ ]` 基于直接 codegen 数据重新评估 loop-header OSR。

## 17. 停止线

第一版在“纯数字函数入口优化 + 安全 Baseline 回退”验收后收口。对象与方法优化、精确 deopt、OSR、函数内联、ARM64 Optimize backend 和外部引擎性能对比不会自动启动；进入这些阶段前分别定义有界目标和验收标准。
