# Baseline 数字循环性能对比

## 测试目标

`examples/baseline_numeric_benchmark.js` 集中覆盖数字 `Add`、`Sub`、`Mul`、`Div`、`Less`、`Greater` 和 `Equal`，用于比较当前普通 VM 与 native baseline 的整体运行成本。它不包含对象、数组或属性访问，避免 inline cache 工作混入数字循环结果。

`numericLoop(5000)` 在每轮计算 `(i * 2 - i) / 2`，执行 `<` 循环条件、`>` 分支和 `==` 分支。外层调用该函数 20 次，共执行 100000 次循环体。

一次调用的计算结果为：

```text
sum(i / 2), i = 0..4999 = 6248750
i > 2500 的次数          = 2499
i == 2500 的次数         = 1
合计                     = 6251250
20 次调用                = 125025000
```

benchmark CLI 将 JIT 调用阈值设为 1，回边阈值设为 1000000。第一次调用安装 baseline code，第二次及后续调用进入 baseline，因此本脚本预期 `baseline_entries=19`。这里只通过函数入口进入机器码，不依赖执行到一半时切换的 OSR。

## 复现方式

使用 Release 构建，避免把未优化 C++ helper 的额外开销当成通常的运行成本：

```powershell
cmake -S . -B build-numeric-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINIJS_BUILD_TESTS=OFF
cmake --build build-numeric-release
.\build-numeric-release\minijs.exe --benchmark-compare-jit examples/baseline_numeric_benchmark.js --benchmark-samples 15 --benchmark-warmup 3 --benchmark-format json
```

以上命令在仓库根目录运行，使用已配置的 C++ 编译器和匹配的运行库。已有 Release 构建目录也可以直接重新构建后运行。

每种 JIT 模式先丢弃 3 轮预热结果，再采样 15 轮，分别取 `compile_us`、`run_us` 中位数。预热与正式采样都创建新的 VM 和 bytecode；预热不会把已编译的 JIT code 传给正式采样。

`compile_us` 计量 AST 到 bytecode 的编译时间；解析发生在采样前。`run_us` 计量 `vm.run()`，包含运行时 JIT 编译、第一调用的普通 VM 执行、后续 native 执行和其中的 helper 调用。该数字不是纯机器码循环的稳态耗时。

JSON 中的 JIT 计数来自最后一个正式样本，不是 15 轮之和；时间是中位数。这些字段不一定来自同一个样本。

## 本机测量

2026-10-11，在 Windows x64、MinGW 13.1、Release `-O3 -DNDEBUG` 下使用 `build-verify-release` 执行了三组上述采样。每组的 JIT on/off 结果均为 `125025000`。

| 采样组 | JIT off 中位数 | JIT on 中位数 | off / on |
| --- | ---: | ---: | ---: |
| 1 | 140.735 ms | 85.714 ms | 1.642x |
| 2 | 130.580 ms | 74.622 ms | 1.750x |
| 3 | 131.107 ms | 82.189 ms | 1.595x |

三组 JIT on 统计均为：

```text
calls=21
backedges=100020
baseline_entries=19
native_entries=2
decoded_entries=0
failed=0
```

`native_entries=2` 表示两个函数拥有 native code object，不表示只执行了两次。实际进入 baseline 的次数由 `baseline_entries` 表示。关闭 JIT 时相关计数为 0。

本次测量显示该数字工作负载约提速 1.60～1.75 倍。它比较的是完整 JIT 开关效果，不能证明某一条快路径单独带来多少收益，也不代表对象、闭包或方法工作负载具有相同加速比。三组波动说明结果仍受本机调度等因素影响。

## 验证与后续方向

CTest 的 `minijs_cli_numeric_benchmark_enters_baseline` 使用一轮无预热 smoke 测试，确认脚本能完成 JIT on/off 对照并产生非零 baseline 入口计数。CLI 自身检查两种模式的计算结果相等。测试不设置耗时或加速比门槛，避免机器性能和调度波动导致失败。

Windows x64 已加入 Number-only `GetLocal` / `SetLocal` 快路径。`GetLocal` 仍由窄 helper 构造压栈槽，随后 native 代码复制 double；`SetLocal` 在源和目标都是数字时直接复制载荷，避免完整 Value 赋值。非数字、类型变化和越界继续调用原来的 helper。

### 数字局部变量修改前后对照

从修改前提交 `6a1b5fe` 隔离构建同编译器、同 `-O3 -DNDEBUG` 配置的可执行文件，与本次修改后的 Release 版本交替执行。每个版本、每组均为 3 轮预热和 15 轮采样。第一、三组先运行修改前版本，第二组先运行修改后版本。

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 110.341 ms | 101.880 ms | 1.083x | 179.899 ms | 178.823 ms |
| 2 | 108.952 ms | 97.631 ms | 1.116x | 191.756 ms | 180.352 ms |
| 3 | 105.083 ms | 95.682 ms | 1.098x | 183.348 ms | 183.548 ms |

三组 JIT on 运行中位数分别降低约 7.7%、10.4%、8.9%。所有运行结果均为 `125025000`，baseline 入口计数仍为 19。本次同机对照支持保留该优化；普通 VM 计时也有波动，数据不能当作其它机器或工作负载上的固定收益。

隔离的修改前构建位于本机忽略的 `build-verify-release/numeric-before-build` 目录，不参与源码提交。最初的三组 JIT 开关数据和本次对照在不同时间执行，绝对耗时不能直接交叉归因；评价本次修改应使用上表的配对数据。

### 数字常量修改前后对照

Windows x64 的数字 `OP_CONSTANT` 现在在编译时把 double 位编码嵌入机器码，运行时构造数字栈槽后直接写入载荷。非数字仍使用完整常量 helper。该路径减少常量池读取和完整 Value 复制，但没有消除栈槽构造的 helper 调用。

对照版本为修改前提交 `64ef036` 的已构建 Release 程序，保存在本机忽略的 `build-verify-release/numeric-constant-before-64ef036.exe`。采样配置仍为每组每版本 3 轮预热、15 轮正式采样，并交替执行前后版本：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 95.623 ms | 96.092 ms | 0.995x | 165.782 ms | 173.779 ms |
| 2 | 135.175 ms | 89.807 ms | 1.505x | 202.129 ms | 180.620 ms |
| 3 | 80.655 ms | 92.046 ms | 0.876x | 176.244 ms | 167.594 ms |

三组结果方向不一致，普通 VM 计时也明显波动。这些数据不足以确认数字常量快路径带来稳定性能收益，不能引用第二组结果宣称提速 1.5 倍。所有结果仍为 `125025000`，baseline 入口计数仍为 19。

本次保留该功能以提供数字常量的 native 位编码写入路径，收益需继续验证。功能测试逐位核对负零、无穷大、最小非规格化数和带 payload 的 NaN，并覆盖连续压栈扩容和混合类型常量。

### OP_POP 无复制栈清理对照

源码检查发现 `OP_POP` 原先调用 `pop()` 取出完整 Value 后立刻丢弃返回值。普通 VM、decoded baseline 和 native runtime helper 现已使用 `discardTop()`，直接执行带空栈检查的 `stack_.pop_back()`。该优化覆盖所有 Value 类型，没有新增数字 guard 或修改机器码调用约定。

对照程序为修改前提交 `144803f` 的 Release 构建，保存在本机忽略的 `build-verify-release/discard-before-144803f.exe`。采样仍为每组每版本 3 轮预热和 15 轮正式采样，第二组交换前后版本的执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 89.086 ms | 75.934 ms | 1.173x | 175.023 ms | 156.800 ms |
| 2 | 100.773 ms | 79.825 ms | 1.262x | 176.120 ms | 181.125 ms |
| 3 | 83.689 ms | 79.331 ms | 1.055x | 171.445 ms | 153.109 ms |

三组 JIT on 中位数分别降低约 14.8%、20.8%、5.2%，结果均为 `125025000`，baseline 入口计数均为 19。JIT off 有两组降低、一组上升，说明波动仍然存在；这不是固定加速比。该测量配合明确减少 Value 复制的源码变化支持保留优化。

测试覆盖三条执行路径丢弃不同类型临时值、GC 后存活闭包继续访问捕获对象，以及普通 VM 和 native helper 的空栈错误。native `OP_POP` 仍调用 runtime helper，helper 成功后刷新 frame 栈快照。

### 数字栈槽原地构造对照

`baselineRuntimePushNumberSlot()` 已从 `push(Value(0.0))` 改为 `stack_.emplace_back(0.0)`。新 Number 直接构造在 vector 的末尾，省去临时 Value 和移动。栈扩容、完整成员初始化和异常处理仍由 C++ 管理，helper 仍刷新栈快照，数字常量和数字 `GetLocal` 共用该优化。普通 VM 和非数字 helper 未调整。

修改前对照程序来自提交 `83ff621` 的 Release 构建，保存为本机忽略的 `build-verify-release/number-slot-before-83ff621.exe`。使用相同的三组交替采样，每组每版本 3 轮预热、15 轮正式采样：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 76.325 ms | 41.596 ms | 1.835x | 164.993 ms | 164.535 ms |
| 2 | 74.866 ms | 41.941 ms | 1.785x | 156.637 ms | 162.377 ms |
| 3 | 77.546 ms | 40.938 ms | 1.894x | 158.402 ms | 162.984 ms |

JIT on 三组运行中位数分别降低约 45.5%、44.0%、47.2%；普通 VM 数据仍在相近范围。所有计算结果为 `125025000`，baseline 入口计数仍为 19。这组数据支持原地构造数字栈槽的收益，不能直接推广为所有脚本的固定加速比。

复用既有回归测试验证数字位编码、连续压栈扩容、递归、类型 fallback 和 GC：CTest 17/17 通过。该变化不增加 opcode、字段布局或新的 Runtime ABI，native 代码仍调用一次栈槽 helper，再重新读取地址并写入数字载荷。

### 布尔条件跳转对照

Windows x64 的 `JumpIfFalse` 现在对 Boolean 直接读取载荷，省去 truthiness helper；非 Boolean 和空栈仍走原有 helper。快路径不弹栈、不修改条件值，返回结果仍遵守 `0/1/2` 三态协议。

修改前程序来自提交 `36abe9f` 的 Release 构建，保存在本机忽略的 `build-verify-release/boolean-branch-before-36abe9f.exe`。每组每版本使用 3 轮预热和 15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 41.091 ms | 43.845 ms | 0.937x | 169.658 ms | 158.525 ms |
| 2 | 44.342 ms | 38.450 ms | 1.153x | 162.485 ms | 159.486 ms |
| 3 | 46.627 ms | 42.446 ms | 1.099x | 165.800 ms | 162.149 ms |

JIT on 一组耗时上升约 6.7%，另两组降低约 13.3%、9.0%；普通 VM 计时也有波动。这些数据不足以证明稳定收益，不能将最好一组当作固定加速比。所有计算结果均为 `125025000`，baseline 入口计数均为 19。

本次保留 Boolean 条件的 native 读取路径，收益仍需后续验证。CTest 17/17 通过，覆盖条件真假、非 Boolean fallback、短路求值保留操作数、循环与空栈错误。Boolean `Not` 原地取反的后续对照见下一节。

### 布尔取反对照

新增 `examples/baseline_boolean_benchmark.js`，在比较结果上执行一次 `!` 和连续两次 `!!`，再通过条件分支累计结果。20 轮、每轮 5000 次循环的预期结果是 `50020`，两种 JIT 配置均核对一致。此样例与数字循环不同，不应交叉比较两者的绝对耗时。

Windows x64 的 Boolean `Not` 通过异或 1 原地翻转载荷，不调用 helper，不改变栈深度。其它类型和空栈继续使用完整 helper。修改前程序来自提交 `cb0f678` 的 Release 构建，保存在本机忽略的 `build-verify-release/boolean-not-before-cb0f678.exe`。以下为重新完成的三组采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 41.555 ms | 26.096 ms | 1.592x | 149.378 ms | 114.204 ms |
| 2 | 40.248 ms | 25.596 ms | 1.572x | 114.286 ms | 114.883 ms |
| 3 | 39.169 ms | 24.888 ms | 1.574x | 113.757 ms | 110.083 ms |

三组 JIT on 中位数降低约 37.2%、36.4%、36.5%。第一组普通 VM 数据也明显波动，因此不能将整个差值都归因于该 opcode；后两组方向一致的数据支持此样例上的收益，但不代表所有脚本都有固定提速。所有计算结果为 `50020`，baseline 入口计数为 19。

功能测试覆盖 decoded/native 取反、非 Boolean fallback、连续取反与分支、实例和函数值、调用副作用以及空栈错误；布尔样例也纳入 CLI baseline smoke 测试。后续 helper 成本采样见下一节。

### 剩余 Helper 热点采样

在 Windows x64 Release 上，对三个样例分别开启 `--benchmark-helper-profile on`，3 轮正式采样、1 轮预热。下表是最后一轮的包含子调用耗时，不是三轮中位数；开启采样会增加开销，数据用于找候选热点而不是宣称加速。

| 样例 | helper | 调用次数 | 包含子调用耗时 |
| --- | --- | ---: | ---: |
| 数字循环 | `PushNumberSlot` | 1,330,133 | 39.952 ms |
| 数字循环 | `SyncStackSize` | 807,538 | 24.758 ms |
| 数字循环 | `Pop` | 522,519 | 15.685 ms |
| 数字循环 | `RecordLoopBackedge` | 95,000 | 2.694 ms |
| 布尔循环 | `PushNumberSlot` | 1,045,133 | 32.043 ms |
| 布尔循环 | `SyncStackSize` | 617,538 | 19.286 ms |
| 布尔循环 | `Pop` | 427,538 | 13.098 ms |
| 综合样例 | `SuperCall` | 299 | 0.217 ms |
| 综合样例 | `Return` | 1,896 | 0.144 ms |
| 综合样例 | `PushNumberSlot` | 3,493 | 0.126 ms |

三个样例的结果分别为 `125025000`、`50020`、`23305`，JIT on/off 均一致；baseline 入口计数分别为 19、19、1896。普通 VM 的 helper 列表为空。数字和布尔样例中没有完整算术或 Boolean `Not` helper 调用，与已实现的内联路径一致。

下一步优先研究数字栈槽构造的 helper 边界：它在两个循环样例中都最频繁。可以先考察批量构造或减少重复压栈的方案，再关闭采样做前后对照。不能为了省一次 helper 直接写入 vector 未构造的容量，也不能跨可能扩容的调用保存旧 `Value*`；GC、Value 生命周期和栈快照契约必须保持。`SyncStackSize` 和 `Pop` 也是候选热点，但暂不同时改动，便于归因。
