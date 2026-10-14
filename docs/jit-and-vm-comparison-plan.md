# MiniJS JIT 与轻量级 JS VM 对比计划

本文是一份总纲式计划，用来把 MiniJS 当前已经完成的 bytecode VM、GC、hidden class、inline cache、Baseline JIT 和后续 Optimize JIT 串成一条清晰路线。同时，它也规划如何与 QuickJS、MuJS、Duktape、JerryScript、Hermes 等轻量级 JavaScript VM 做对比。

状态校准：2026-10-14。Windows x64 Baseline 已收口，进入功能冻结与维护；ARM64 后续验收由另一台机器负责，本机暂不处理。Optimize JIT 和外部 VM 对比仅为未立项候选，不由普通“下一步”自动启动。本文不是新增功能清单、版本承诺或全平台验收声明。

相关专题文档：

- Baseline JIT 设计：[baseline-jit.md](baseline-jit.md)
- 已完成验收与停止线：[baseline-jit-closeout.md](baseline-jit-closeout.md)
- 已验证演示流程：[project-demo.md](project-demo.md)
- Optimize JIT 设计：[optimizing-jit.md](optimizing-jit.md)
- QuickJS 架构理解：[quickjs-architecture-guide.md](quickjs-architecture-guide.md)
- Hidden class：[hidden-class.md](hidden-class.md)
- Inline cache：[inline-cache.md](inline-cache.md)
- Runtime / GC：[runtime.md](runtime.md)、[gc.md](gc.md)

## 1. 项目定位

MiniJS 不需要追求完整 ECMAScript 兼容，也不应该把目标定成复刻 QuickJS。更合适的定位是：

```text
一个可解释、可展示、可逐层优化的小型 JavaScript VM
```

核心价值不是语法覆盖量，而是把现代 JS 引擎的关键机制做成可读、可测、可演进的版本：

- Parser / AST / bytecode compiler
- Stack VM
- Runtime object model
- GC-managed heap object
- Closure / upvalue / class / method / super
- Hidden class
- Inline cache
- Baseline JIT
- Optimize JIT 设计
- 与轻量级 JS VM 的架构对比

最终展示重点应该是：

```text
MiniJS = 教学型 JS VM + 真实运行时对象系统 + 可运行 Baseline JIT + 可设计 Optimize JIT
```

## 2. 分层路线

MiniJS 后续可以按四层组织：

```text
AST Interpreter
  -> Bytecode VM
  -> Baseline JIT
  -> Optimize JIT
```

每层目标不同。

| 层级 | 目标 | 当前状态 | 展示价值 |
| --- | --- | --- | --- |
| AST Interpreter | 验证语言语义 | 已完成主体 | 解释器基础 |
| Bytecode VM | 稳定执行与运行时模型 | 已完成主体 | VM / GC / class / closure |
| Baseline JIT | 缓存解码结果，生成机器码或 helper stub | Windows x64 已收口，功能冻结 | tiering、ABI、native fast path |
| Optimize JIT | 使用反馈做类型专门化和 deopt | 仅设计，未实现、未立项 | 现代 JS 引擎思路 |

Baseline JIT 不要求所有 opcode 都机器码化。第一目标是建立稳定 tiering 和执行入口；第二目标才是挑高收益路径做 native fast path。

## 3. Baseline JIT 计划

### 3.1 已完成能力

当前 Baseline JIT 已经具备：

- 函数调用和循环回边热度统计。
- `JitFeedback` 状态管理。
- bytecode verifier / decoder。
- decoded baseline executor。
- native baseline entry。
- Windows x64 / ARM64 stub backend。
- Runtime ABI helper。
- 构建后的可执行内存管理。
- 普通函数调用、方法调用、`super`、closure、class-family opcode 的 helper 路径。
- Windows x64 number fast path。
- 局部槽、赋值合并、条件跳转、benchmark 路径优化。

### 3.2 Baseline JIT 冻结边界

以下分类解释现有实现与未来设计的边界，不要求继续扩展 Baseline。维护只接受正确性、ABI/GC 安全、可复现性能回归及构建/文档修复。

第一类：保留现有 native fast path，不再作为追加开发任务。

| Opcode / 场景 | 现有快路径方向（不保证所有组合内联） |
| --- | --- |
| `SetLocal` / local copy | 减少临时栈槽和 helper 调用 |
| `Equal` / `Greater` / `Less` | number / boolean fast path |
| `JumpIfFalse` / `Loop` | 低成本控制流 |
| 局部变量数字更新 | 支持常见 loop counter |
| 数字常量赋值 / 返回 | 消除临时 `Value` 移动 |

第二类：保持 runtime helper 边界，不要求全部内联。

| Opcode / 场景 | 原因 |
| --- | --- |
| `Call` | 涉及新 call frame、返回槽、native/builtin 分派 |
| `MethodCall` | receiver、class、static、array builtin、method IC |
| `SuperCall` | superclass、receiver 重排、继承链查找 |
| `Closure` | upvalue 捕获和 GC root 安全 |
| `Class` / `Method` / `StaticMethod` / `Inherit` | 类对象和方法表语义复杂 |
| `Object` / `GetProperty` / `SetProperty` | 未来可用 shape feedback 优化 |

第三类：若另行立项，可考虑作为 Optimize JIT 输入；不等于当前已有优化器或 deopt 反馈。

| 反馈 | 后续用途 |
| --- | --- |
| property inline cache | shape guard + slot load/store |
| method inline cache | class guard + direct closure call |
| helper hot profile | 找出最值得 native 化的 opcode |
| baseline entry count | 判断是否进入 Optimize JIT |
| deopt / fallback count（未来设计项） | 需另行定义计数与恢复协议，不能以现有 helper 回退等同 deopt |

### 3.3 Baseline JIT 已完成验收

Windows x64 已按 [收口记录](baseline-jit-closeout.md) 完成普通和 Release 两套 CTest（各 43/43），以及跨功能样例结果一致性验证。验收范围包括：

- `ctest` 全绿。
- CLI benchmark 覆盖：
  - numeric loop
  - boolean branch
  - local copy
  - constant assignment
  - arithmetic assignment
  - comparison assignment
  - property / method IC benchmark
- 文档说明：
  - native fast path 与 helper fallback 的边界
  - stack sync 规则
  - Runtime ABI
  - decoded executor 与 native entry 的关系
- `--benchmark-compare` 单独比较 IC 开关，`--benchmark-compare-jit` 单独比较 JIT 开关；它们不是 Interpreter/VM/IC/JIT 四层的一次性比较。
- 对暂不 native 化的 opcode 有明确说明。

原建议阶段 tag（尚未创建，不表示实际项目版本）：

```text
v0.10-baseline-jit
```

## 4. Optimize JIT 设计计划

本节是未来候选，不在当前交付范围内。真正启动前须另定一个有界目标和验收标准；不要求一次完成下面全部能力。

Optimize JIT 不应该直接替代 Baseline JIT。它应该在 Baseline JIT 稳定之后，复用 bytecode、feedback 和 baseline 执行计数。

### 4.1 第一版目标

第一版 Optimize JIT 只做函数入口优化，不做 OSR。

目标：

- 读取 bytecode 和 `DecodedInstruction`。
- 建立 basic block 和 CFG。
- 做 stack-to-SSA。
- 附加 type / shape / method feedback。
- 插入 guard。
- 生成简单 optimized IR。
- 失败时 deopt 回 baseline。

第一版可优化场景：

| 场景 | 优化方式 |
| --- | --- |
| number-only 算术 | 去掉重复 `Value` 类型判断 |
| number comparison | 直接 double compare |
| monomorphic property read | shape guard + slot load |
| monomorphic property write | shape guard + slot store |
| simple method call | class guard + direct closure |
| hot local loop | 减少 stack push/pop |

### 4.2 需要新增的核心模块

建议文件：

```text
include/minijs/optimizer_ir.h
src/optimizer_ir.cpp
include/minijs/optimizing_jit.h
src/optimizing_jit.cpp
include/minijs/deoptimizer.h
src/deoptimizer.cpp
include/minijs/optimized_code.h
src/optimized_code.cpp
```

核心结构：

```text
Bytecode
  -> DecodedInstruction
  -> CFG
  -> SSA IR
  -> Guarded IR
  -> Lowered IR
  -> OptimizedCode
```

### 4.3 Deopt 与 safepoint

Optimize JIT 的最大风险不是生成机器码，而是失败后如何回到正确状态。

第一版 deopt 可以做得保守：

- 只允许函数入口进入 optimized code。
- 只有入口 guard 在任何可观察副作用发生前失败，才可以回 baseline 函数入口执行。
- 执行后的 guard 失败不能无条件重跑入口，否则会重复属性写入、全局更新、调用或其它副作用；此时必须有对应 bytecode offset 的 VM 状态恢复方案，或明确不支持该优化场景。
- 不做中途恢复到任意 bytecode offset。
- 不做 OSR。
- 不做跨函数内联。

第二版再增加：

- bytecode offset 到 VM state 的映射。
- stack slot / local slot materialization。
- loop OSR entry。
- guard failure reason profiling。

### 4.4 Optimize JIT 收口条件

第一版 Optimize JIT 可交付条件：

- 有独立 `OptimizedCode` 表示。
- 至少支持 number-only arithmetic function。
- 至少支持 monomorphic property read。
- guard 失败能安全回 baseline。
- benchmark 能展示：
  - VM
  - Baseline JIT
  - Optimize JIT
- 文档能讲清楚：
  - feedback 来源
  - guard 插入
  - deopt 边界
  - 为什么不做完整 V8 级优化

原建议阶段 tag（仅候选，尚未创建）：

```text
v0.11-optimize-jit-prototype
```

## 5. 与轻量级 JS VM 的对比计划

本节保留原规划中的对比维度，不代表已执行外部引擎测试。实际开展时需先固定各引擎版本、引用其官方实现/文档，并确认样例属于双方支持的语言子集；本文不提供新的性能或兼容性结论。

对比不应该只比性能。MiniJS 是学习型项目，更适合从架构维度比较。

### 5.1 对比对象

| 引擎 | 定位 | 适合作为对比点 |
| --- | --- | --- |
| QuickJS | 小型完整 JS 引擎 | bytecode VM、RC GC、完整语言特性、C API |
| MuJS | 小型嵌入式 JS 引擎 | 简洁实现、解释器/对象模型 |
| Duktape | 嵌入式 JS 引擎 | C API、低资源环境、可移植性 |
| JerryScript | IoT JS 引擎 | 小内存、嵌入式、字节码执行 |
| Hermes | 移动端 JS 引擎 | bytecode ahead-of-time、启动性能 |
| MiniJS | 教学型 VM + JIT 实验 | 可解释架构、渐进式 JIT、可读性 |

QuickJS 是最重要的主对比对象；其它引擎用于横向说明不同取舍。

### 5.2 对比维度

| 维度 | MiniJS 关注点 |
| --- | --- |
| 语言覆盖 | MiniJS 只覆盖核心 JS 子集 |
| 前端 | AST 保留更清晰；QuickJS 更紧凑地直接编译到 bytecode |
| Bytecode | 栈式 VM，便于实现和调试 |
| Runtime value | 动态 `Value`，逐步接近真实 JS runtime |
| 对象模型 | hidden class + slots + dictionary fallback |
| GC | mark-sweep GC；QuickJS 是 RC + cycle removal |
| 闭包 | upvalue 捕获和关闭 |
| class/method/super | 支持核心 class 行为 |
| IC | property / method inline cache |
| Baseline JIT | MiniJS 的主要差异化展示点 |
| Optimize JIT | 仅设计，未实现原型，不追求完整工业级 |
| C API / embedding | 非核心目标 |
| 模块 / Promise / async | 暂不作为主线 |

### 5.3 QuickJS 对比重点

可以写成一章：

```text
MiniJS vs QuickJS
```

建议结构：

1. 总体定位：
   - QuickJS：完整、紧凑、可嵌入。
   - MiniJS：教学、可解释、可逐层演进。
2. 前端：
   - QuickJS 直接生成 bytecode。
   - MiniJS 保留 AST，再编译 bytecode，便于教学和测试。
3. VM：
   - 二者都可视为 bytecode VM。
   - MiniJS 额外展示 decoded baseline / native baseline。
4. Runtime：
   - QuickJS runtime 覆盖完整 ECMAScript。
   - MiniJS runtime 只做核心对象、数组、函数、class。
5. GC：
   - QuickJS：引用计数 + 循环处理。
   - MiniJS：标记清扫，更容易讲清 root tracing。
6. JIT：
   - QuickJS 主线不依赖 JIT。
   - MiniJS 把 JIT 当作学习现代引擎 pipeline 的重点。
7. 适用场景：
   - QuickJS：嵌入式运行完整 JS。
   - MiniJS：展示编译器、VM、runtime 和 JIT 原理。

### 5.4 性能对比原则

如果做 benchmark，必须避免把结论写成“MiniJS 比 QuickJS 快”。MiniJS 的语言覆盖和优化范围都不同，直接比总性能意义不大。

推荐对比方式：

- 同一小程序在 MiniJS 不同 tier 之间对比：
  - VM
  - VM + IC
  - Baseline JIT
  - Optimize JIT prototype（未来候选，当前不可运行）
- MiniJS 与 QuickJS 对比时，只观察数量级和取舍：
  - 启动时间
  - 简单循环
  - 函数调用
  - 对象属性访问
  - 方法调用
  - 内存占用
- 结论重点放在架构解释，不放在绝对胜负。

示例 benchmark 分类：

| 分类 | 示例 |
| --- | --- |
| numeric | `sum += i * 2` |
| boolean | branch-heavy loop |
| local | local copy / assign / return |
| property | monomorphic object property read/write |
| method | repeated method call |
| closure | closure counter |
| class | constructor + method |

## 6. 文档与展示结构

最终可以整理成以下文档体系：

```text
README.md
  -> docs/runtime.md
  -> docs/gc.md
  -> docs/hidden-class.md
  -> docs/inline-cache.md
  -> docs/baseline-jit.md
  -> docs/optimizing-jit.md
  -> docs/jit-and-vm-comparison-plan.md
  -> docs/quickjs-architecture-guide.md
```

README 中只保留总览；细节放专题文档。

展示时建议按这条线讲：

```text
Source
  -> AST
  -> Bytecode
  -> VM
  -> Runtime / GC
  -> Hidden class / IC
  -> Baseline JIT
  -> Optimize JIT
  -> QuickJS 对比
```

## 7. 版本路线建议

下表保留原版本命名设想，不是已创建 tag 或发布计划；当前不修改 CMake 项目版本、不自动打 tag。尤其不再通过 `v0.10.1` 名义追加 Baseline 微基准任务。

| 版本 | 目标 |
| --- | --- |
| `v0.9-vm-default-cli` | VM 成为默认执行路径 |
| `v0.10-baseline-jit` | Baseline JIT 稳定，benchmark 和文档齐备 |
| `v0.10.1-jit-benchmark` | 补 benchmark、helper profile、性能报告 |
| `v0.11-optimize-jit-prototype` | 第一版 Optimize JIT IR / guard / deopt 原型 |
| `v1.0-engine-study` | 文档、对比、测试、示例整理后的完整展示版 |

## 8. 当前停止线与候选阶段

当前已完成 Baseline 修复收口、设计与性能文档整理、README 入口和可复现演示指南。历史性能数据对应各自提交，不再为当前收口补新微基准或承诺固定提速比。

当前阶段没有必须继续增加的引擎功能。保留现有 helper 和快路径，仅按复现问题进入维护；ARM64 交由另一台机器，本机不再推进。

后续候选只有在用户明确立项后才能开始：

1. Optimize JIT：先约定最小场景、guard 位置、回退安全性和验收测试，再决定是否建立 IR；不默认同时承诺属性专门化、OSR 和 deopt。
2. 外部 VM 架构对比：先选一个引擎和固定版本，限定章节范围并核对官方来源；不默认下载、运行外部引擎或做性能排名。
3. 发布整理：先明确版本、tag 和发布范围；不因文档收口自动发布。

进入任何候选阶段前，都需另行确认范围和验收标准。当前文档整理完成即停止，不再从本计划自动派生功能任务。
