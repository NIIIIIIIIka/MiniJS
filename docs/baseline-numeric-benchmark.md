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

由此选择相邻局部值与数字常量的合并压栈实验，见下一节。不能为了省一次 helper 直接写入 vector 未构造的容量，也不能跨可能扩容的调用保存旧 `Value*`；GC、Value 生命周期和栈快照契约必须保持。

### 相邻压栈合并对照

Windows x64 将非独立跳转入口的 `GetLocal + 数字 Constant` 合并为一次 `GetLocalAndPushNumberSlot`，正常构造两个 Value 后由机器码写入常量载荷。修改前程序来自提交 `d61260e` 的 Release 构建，保存在本机忽略的 `build-verify-release/paired-push-before-d61260e.exe`。

性能对照关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序，仍使用数字循环样例：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 30.186 ms | 30.489 ms | 0.990x | 111.943 ms | 113.344 ms |
| 2 | 30.605 ms | 30.029 ms | 1.019x | 114.297 ms | 110.982 ms |
| 3 | 31.488 ms | 32.431 ms | 0.971x | 112.605 ms | 116.679 ms |

三组 JIT on 耗时分别上升约 1.0%、降低约 1.9%、上升约 3.0%，普通 VM 数据也有波动，不能宣称稳定提速。所有结果为 `125025000`，baseline 入口计数为 19。

另行开启采样、单轮执行同一脚本核对调用数：原先 `PushNumberSlot` 为 1,330,133 次；修改后为 855,095 次，新增合并 helper 237,519 次。两类压栈 helper 合计 1,092,614 次，减少 237,519 次，约 17.9%。这里没有减少 Value 构造数量，只减少了 C ABI 调用和栈快照刷新次数；调用数减少不等于整体耗时同比下降。

保留已验证的合并机制，但不将它作为已证实的性能提升。下一步应重新测量 `SyncStackSize` 的边界成本，或研究更直接地减少临时栈槽的方案；继续用关闭采样的配对数据判断收益。

### 单槽同步专门化实验（未保留）

候选实现只修改 `baselineRuntimeSyncStackSize()`：目标深度恰好比真实栈少 1 时用 `pop_back()`，否则仍用 `resize()`。没有修改 ABI、生成机器码或 helper 调用次数，Value 仍正常析构。

修改前为提交 `b096576` 的 Release 程序，保存为本机忽略的 `build-verify-release/single-pop-before-b096576.exe`。对照数字循环，关闭 helper 采样，每组每版本 3 轮预热、15 轮采样，第二组交换执行顺序：

| 对照组 | 原实现 JIT on | 候选 JIT on | 原 / 候选 | 原实现 JIT off | 候选 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 33.710 ms | 30.871 ms | 1.092x | 118.829 ms | 115.317 ms |
| 2 | 31.485 ms | 30.365 ms | 1.037x | 114.043 ms | 112.443 ms |
| 3 | 30.385 ms | 34.352 ms | 0.885x | 144.368 ms | 111.204 ms |

JIT on 两组降低约 8.4%、3.6%，一组增加约 13.1%；普通 VM 数据也有明显波动。所有结果均为 `125025000`，baseline 入口计数均为 19，不能把某一组结果当作稳定提速。

决定不保留候选分支，最终代码继续使用通用 `resize()`。保留新增的单槽、多槽、不变深度、清空与超界错误的直接 C ABI 测试；原实现和候选实现均通过 19 项 CTest。下一步应研究更实质地减少临时栈槽或同步调用次数，先明确 GC 与 helper 边界，再实施。

### 常量二元运算合并对照

Windows x64 合并数字 `Constant` 与紧邻的 `Add/Sub/Mul/Div/Equal/Greater/Less`。Number guard 成功时直接用寄存器常量更新原栈顶，不构造右操作数临时槽，也不调用 `SyncStackSize`。guard 未命中时仍按顺序构造常量并调用完整运算 helper；独立分支入口和零除数不合并。

修改前来自提交 `275990a` 的 Release 程序，保存在本机忽略的 `build-verify-release/constant-binary-before-275990a.exe`。下面是构建和测试完成后单独运行的三组对照，关闭 helper 采样，每组每版本 3 轮预热、15 轮采样，第二组交换执行顺序。早期与构建/测试重叠的探索采样不用于本表。

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 31.939 ms | 25.873 ms | 1.234x | 120.090 ms | 117.866 ms |
| 2 | 33.070 ms | 25.680 ms | 1.288x | 116.095 ms | 115.487 ms |
| 3 | 31.897 ms | 25.326 ms | 1.259x | 119.627 ms | 115.987 ms |

三组 JIT on 中位数分别降低约 19.0%、22.3%、20.6%。所有结果为 `125025000`，baseline 入口计数为 19。这组同机配对数据支持保留该优化，不代表所有工作负载都有固定收益。

另外单轮开启采样，仅核对调用次数，不用其耗时推算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 855,095 | 997,614 |
| `GetLocalAndPushNumberSlot` | 237,519 | 0 |
| `SyncStackSize` | 807,538 | 475,019 |

新的常量运算组合优先于旧的相邻压栈组合，因此不能只看 `PushNumberSlot` 一项：两类数字压栈 helper 的合计从 1,092,614 降到 997,614，减少 95,000 次；同步调用减少 332,519 次。原先合并 helper 构造两个槽，新路径还实际省掉常量临时 Value，区别于只合并 C ABI 调用。

CTest 19/19 通过。专项测试覆盖全部七种运算、特殊浮点数与负零符号、NaN 有序比较、非数字相等与字符串拼接、类型错误、除零、空栈和独立运算跳转入口。下一步可研究条件比较与分支的组合，但先保留当前真假值和短路表达式的栈契约。

### 比较与条件跳转合并对照

Windows x64 合并 `Equal/Greater/Less + JumpIfFalse`，包括数字常量比较的三条组合。Boolean 栈槽仍保留；新路径只减少条件值的重复读取或 guard，不减少比较同步、条件弹栈等 helper 调用。常量快路径复用寄存器中的真假结果，fallback 则在比较成功后读取 Boolean，失败仍通过 epilogue 传播。

修改前来自提交 `d6e7a51` 的 Release 程序，保存为本机忽略的 `build-verify-release/comparison-branch-before-d6e7a51.exe`。数字循环样例关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 29.866 ms | 40.842 ms | 0.731x | 170.887 ms | 158.803 ms |
| 2 | 44.240 ms | 33.157 ms | 1.334x | 151.211 ms | 152.034 ms |
| 3 | 33.108 ms | 35.990 ms | 0.920x | 167.037 ms | 184.870 ms |

三组方向不一致，普通 VM 计时也明显波动，不能据此确认稳定性能收益，更不能只引用第二组宣称加速 1.334 倍。所有计算结果均为 `125025000`，baseline 入口计数均为 19。

本次保留已验证的比较分支合并机制，但不将其列为已证明的提速。CTest 19/19 通过，专项测试覆盖合并映射、真假条件、NaN、类型 fallback 和错误传播、短路表达式的值与副作用，以及独立条件跳转入口。后续若要消除条件 Value 或其 `Pop`，必须先证明条件结果不会被返回或由 `&&`、`||` 保留，不能仅凭相邻 opcode 就删除栈值。

### 局部数字更新语句对照

Windows x64 对同槽 `GetLocal + 数字 Constant + 算术 + SetLocal + Pop` 直接原地更新局部 Number。只在结果被丢弃、内部指令无独立跳转入口且除数非零时匹配；fallback 保留逐步读取、运算、赋值和丢弃的行为。

修改前来自提交 `d1ebb89` 的 Release 程序，保存为本机忽略的 `build-verify-release/local-update-before-d1ebb89.exe`。数字循环关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 25.764 ms | 25.883 ms | 0.995x | 117.866 ms | 117.567 ms |
| 2 | 26.081 ms | 24.161 ms | 1.079x | 122.261 ms | 120.620 ms |
| 3 | 39.634 ms | 25.024 ms | 1.584x | 124.152 ms | 121.794 ms |

JIT on 分别增加约 0.5%、降低约 7.4%、降低约 36.9%。第三组修改前计时明显偏离其它组，不能据此宣称稳定提速 1.584 倍。所有结果为 `125025000`，baseline 入口计数为 19。该实验确认调用数减少，但仍需更长时间的配对数据验证稳定收益。

单独开启采样、单轮执行核对调用次数：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 997,614 | 855,114 |
| `Pop` | 522,519 | 380,019 |
| `SyncStackSize` | 475,019 | 475,019 |

压栈与 Pop 分别减少 142,500 次，合计减少 285,000 次 helper 调用。本次未减少同步次数，因为上一步的常量二元合并已经消除了这些更新里的临时常量槽及同步。采样耗时不用于判断加速比。

CTest 19/19 通过。测试覆盖合并映射、四种算术、NaN/无穷大/非规格化数/负零、字符串 fallback、赋值结果被返回与不同目标槽、闭包捕获和 GC 后访问、内部跳转入口、零除数及非法局部槽。

### 双局部槽更新对照

新增 `examples/baseline_local_pair_benchmark.js`，核心循环是 `total = total + i;`，执行 20 轮、每轮 5000 次。预期结果为 `249950000`。它与前面的数字循环不是同一工作负载，不能交叉比较绝对耗时。

Windows x64 对同目标的 `GetLocal + GetLocal + 算术 + SetLocal + Pop` 直接更新左局部 Number，不构造两个临时值，不同步弹栈，也不调用末尾 Pop。动态除数零、非数字和边界错误仍走完整 helper。修改前来自提交 `69ba433` 的 Release 程序，保存为本机忽略的 `build-verify-release/local-pair-update-before-69ba433.exe`。

下表关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 10.413 ms | 6.570 ms | 1.585x | 51.519 ms | 59.259 ms |
| 2 | 10.568 ms | 6.760 ms | 1.563x | 49.997 ms | 58.010 ms |
| 3 | 10.232 ms | 6.644 ms | 1.540x | 51.081 ms | 49.879 ms |

三组 JIT on 中位数降低约 36.9%、36.0%、35.1%。普通 VM 数据仍有波动，这不是所有脚本的固定加速比；三组同机配对数据支持保留该快路径。所有计算结果均为 `249950000`，baseline 入口计数均为 19。

单独开启采样、单轮运行，仅核对调用次数：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 380,095 | 190,095 |
| `SyncStackSize` | 190,019 | 95,019 |
| `Pop` | 190,019 | 95,019 |

分别减少 190,000 次压栈、95,000 次同步和 95,000 次 Pop，合计减少 380,000 次 helper 调用。这是 19 次 baseline 入口中的 95,000 次累加更新，首轮仍在普通 VM 执行。采样耗时不用于计算收益。

CTest 20/20 通过，新增样例有 CLI baseline smoke 测试；专项测试覆盖全部四种算术、特殊浮点数、左右同槽、类型切换、动态除零、赋值值保留、闭包/GC、独立内部入口和非法右局部槽。

### 双局部值结果表达式对照

新增 `examples/baseline_local_expression_benchmark.js`，核心循环为 `total = total + (i + offset);`，执行 20 轮、每轮 5000 次，offset 为 2，预期结果为 `250150000`。它与此前各样例不同，不能交叉比较绝对耗时。

Windows x64 合并 `GetLocal + GetLocal + 二元运算`，两个源值均为 Number 时只构造一个结果槽，不再复制两个临时操作数再同步弹栈。支持四种算术和三种比较，五条原地更新语句仍优先。修改前来自提交 `435c976` 的 Release 程序，保存为本机忽略的 `build-verify-release/local-expression-before-435c976.exe`。

下表关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 12.670 ms | 9.190 ms | 1.379x | 61.429 ms | 70.597 ms |
| 2 | 12.657 ms | 9.205 ms | 1.375x | 65.434 ms | 69.295 ms |
| 3 | 12.705 ms | 9.041 ms | 1.405x | 58.767 ms | 62.399 ms |

三组 JIT on 中位数降低约 27.5%、27.3%、28.8%。普通 VM 数据仍有波动，但三组同机 JIT 对照方向一致，支持保留该快路径；不能推广为所有脚本的固定收益。所有计算结果为 `250150000`，baseline 入口计数为 19。

另行单轮开启采样核对调用数，不用采样耗时计算提速：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 475,114 | 285,095 |
| `SyncStackSize` | 285,019 | 95,000 |
| `Pop` | 190,019 | 190,019 |

压栈和同步各减少 190,019 次，合计减少 380,038 次 helper 调用。结果仍保留，条件或语句末尾的 Pop 未消除。减少量包括内部 `i + offset` 和循环条件 `i < count` 的双局部值组合。

CTest 21/21 通过，新增样例已纳入 CLI baseline smoke；专项测试覆盖所有七种运算、NaN/无穷大/非规格化数/负零、源值不变、别名、连续 80 个结果槽的扩容、类型 fallback、GC 与对象身份、除零、非法槽和独立运算入口。

### 直接返回局部值对照

新增 `examples/baseline_local_return_benchmark.js`，通过 `identity(value) { return value; }` 的 100,000 次调用累计结果，20 轮、每轮 5000 次，预期结果为 `249950000`。该样例专门观察调用与局部返回，不应与其它循环的绝对耗时交叉比较。

Windows x64 将非独立 Return 入口的 `GetLocal + Return` 合并为 `ReturnLocal` helper，省去临时返回值压栈。所有 Value 类型沿用正常复制，普通返回和局部直接返回共享 upvalue 关闭、receiver 和返回槽整理。修改前来自提交 `7bacecc` 的 Release 程序，保存为本机忽略的 `build-verify-release/local-return-before-7bacecc.exe`。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 30.392 ms | 29.814 ms | 1.019x | 87.870 ms | 82.739 ms |
| 2 | 31.845 ms | 32.300 ms | 0.986x | 74.266 ms | 77.382 ms |
| 3 | 31.102 ms | 33.387 ms | 0.932x | 85.546 ms | 104.413 ms |

JIT on 一组降低约 1.9%，另两组增加约 1.4%、7.3%；普通 VM 数据也有波动。不能据此宣称稳定提速。所有计算结果为 `249950000`，baseline 入口计数为 100,018。

单独开启采样、单轮核对调用次数：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 385,075 | 285,057 |
| `Return` | 100,018 | 0 |
| `ReturnLocal` | 0 | 100,018 |

数字压栈减少 100,018 次，返回 helper 的数量未变，但换成不需要临时栈槽的合并入口。减少量包括 identity 的 99,999 次 baseline 返回及循环函数的 19 次 baseline 返回。调用数减少不等于整体耗时同比下降，采样耗时不用于计算收益。

本次保留已验证的直接局部返回机制和统一收尾逻辑，不将它列为已证实的提速。CTest 22/22 通过；专项测试覆盖各类 Value、身份、闭包/GC、方法 this、receiver 返回规则、合并计数、独立 Return 入口与非法局部槽错误。

### 数字常量直接返回对照

新增 `examples/baseline_number_return_benchmark.js`，通过 `one() { return 1; }` 的 100,000 次调用累加结果，20 轮、每轮 5000 次，预期结果为 `100000`。这是单独工作负载，不与其它循环的绝对耗时交叉比较。

Windows x64 合并数字 `Constant + Return`，用 uint64_t 参数传递原始 double 位编码并直接进入共享返回收尾，不构造临时常量栈槽。非数字常量和独立 Return 入口保持原路径。修改前来自提交 `25423d8` 的 Release 程序，保存为本机忽略的 `build-verify-release/number-return-before-25423d8.exe`。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 32.148 ms | 28.892 ms | 1.113x | 90.363 ms | 83.259 ms |
| 2 | 29.944 ms | 26.361 ms | 1.136x | 73.648 ms | 87.438 ms |
| 3 | 31.988 ms | 26.900 ms | 1.189x | 74.282 ms | 74.287 ms |

三组 JIT on 中位数降低约 10.1%、12.0%、15.9%。普通 VM 数据仍有波动；三组同机对照支持保留该合并，但不是所有脚本的固定加速比。所有结果均为 `100000`，baseline 入口计数为 100,018。

另行单轮开启采样，仅核对调用次数，不用采样耗时计算提速：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 290,056 | 190,057 |
| `Return` | 99,999 | 0 |
| `ReturnNumberBits` | 0 | 99,999 |
| `ReturnLocal` | 19 | 19 |

数字压栈减少 99,999 次；数字返回 helper 替换原 Return，局部返回 helper 不变。该数字返回已经合并，但仍执行正常 VM 返回收尾，不是直接机器 ret。

CTest 23/23 通过。逐位测试覆盖负零、无穷大、最小非规格化数和不同 NaN payload，并确认新 helper 计数；专项测试还覆盖关闭捕获槽及 GC、方法常量、非数字常量、receiver 规则、返回槽错误和独立 Return 入口。

### 文字常量直接返回对照

新增 `examples/baseline_literal_return_benchmark.js`，通过 `label() { return "ok"; }` 和 `len(label())` 的 100,000 次调用累计字符串长度。20 轮、每轮 5000 次，预期结果为 `200000`。样例包含函数调用、内置 len、字符串创建和 GC，不能把整个耗时当作单一返回 opcode 的成本，也不与其它样例交叉比较。

Windows x64 合并字符串、Boolean、null/undefined 的 `Constant + Return` 为 `ReturnConstant`。字符串仍正常创建 ObjString，以 TemporaryRootScope 保护到进入调用方返回槽；数字位编码 helper 保持优先。修改前来自提交 `1d11dfa` 的 Release 程序，保存为本机忽略的 `build-verify-release/literal-return-before-1d11dfa.exe`。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 61.271 ms | 58.246 ms | 1.052x | 92.645 ms | 95.046 ms |
| 2 | 64.692 ms | 59.304 ms | 1.091x | 95.442 ms | 95.171 ms |
| 3 | 61.566 ms | 59.259 ms | 1.039x | 93.841 ms | 95.551 ms |

三组 JIT on 中位数降低约 4.9%、8.3%、3.7%。同机配对数据支持保留该合并；收益较小且仍存在系统噪声，不是所有脚本的固定加速比。所有结果为 `200000`，baseline 入口计数为 100,018。

另行单轮开启采样，只核对调用数，不用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushConstant` | 99,999 | 0 |
| `Return` | 99,999 | 0 |
| `ReturnConstant` | 0 | 99,999 |
| `ReturnLocal` | 19 | 19 |

两类旧 helper 合并为一次返回 helper，总调用减少 99,999 次。字符串分配数量没有因合并而消失；临时根替代了旧临时栈槽的保护。

CTest 24/24 通过。测试覆盖各类文字常量、数字路径优先级、合并映射和计数、长字符串循环返回后的回收、异常路径的临时根释放、捕获对象和 GC、方法、receiver、索引错误及独立 Return 入口。

### 局部复制赋值对照

新增 `examples/baseline_local_copy_benchmark.js`，循环执行 `copied = i; total = total + copied;`，20 轮、每轮 5000 次，预期结果 `249950000`。Windows x64 合并结果被丢弃的 GetLocal + SetLocal + Pop；数字到数字直接复制载荷，其它类型用一次完整 Value 复制 helper。修改前来自提交 `d3de967`，保存为本机忽略的 `build-verify-release/local-copy-before-d3de967.exe`。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序。每次采样使用新的 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 7.297 ms | 5.579 ms | 1.308x | 56.953 ms | 67.466 ms |
| 2 | 7.064 ms | 5.427 ms | 1.302x | 56.685 ms | 56.012 ms |
| 3 | 7.301 ms | 5.629 ms | 1.297x | 58.366 ms | 56.193 ms |

三组 JIT on 中位数降低约 23.5%、23.2%、22.9%。普通 VM 尤其第一组仍有系统噪声；这是该复制循环的同机专项收益，不是任意脚本的固定加速比。所有计算结果为 `249950000`，baseline 入口计数为 19。

另行单轮开启采样，只核对调用次数：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 190,076 | 95,076 |
| `Pop` | 190,019 | 95,019 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |
| `CopyLocal` | 0 | 0 |

数字复制在机器码内完成，CopyLocal fallback 未命中；总计减少 190,000 次 helper 调用。循环条件仍构造 Boolean 结果槽并 Pop，没有将剩余调用归零。采样耗时不用于计算提速。

CTest 25/25 通过。专项测试覆盖所有 Value 类别、数字位编码、类型切换、自赋值、赋值结果保留、对象/闭包/实例身份、捕获槽与 GC、独立中间入口、非法槽，以及 GetLocal 新增临时槽作为目标时的原始行为和失败栈状态。

### 数字常量赋值对照

新增 `examples/baseline_constant_assign_benchmark.js`，循环执行 `value = 7; total = total + value;`，20 轮、每轮 5000 次，预期结果 `700000`。Windows x64 合并 Number Constant + SetLocal + Pop；数字目标直接写入常量位编码，其它旧类型由完整 Value 赋值 helper 处理。修改前来自提交 `1da7620`，保存为本机忽略的 `build-verify-release/constant-assign-before-1da7620.exe`。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序；每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 6.991 ms | 5.578 ms | 1.253x | 57.872 ms | 58.190 ms |
| 2 | 7.023 ms | 5.341 ms | 1.315x | 56.150 ms | 55.323 ms |
| 3 | 7.226 ms | 5.596 ms | 1.291x | 55.811 ms | 55.506 ms |

三组 JIT on 中位数降低约 20.2%、24.0%、22.6%。这是同机数字常量赋值循环的专项收益，不是任意脚本的固定加速比，普通 VM 与系统调度仍存在噪声。所有结果为 `700000`，baseline 入口计数为 19。

另行单轮开启采样，仅核对调用次数，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 190,076 | 95,076 |
| `Pop` | 190,019 | 95,019 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |
| `AssignConstant` | 0 | 0 |

数字目标命中快路径，总计少 190,000 次 helper 调用；循环条件仍保留结果槽和 Pop。类型 fallback 不由本微基准计数，另由专项测试验证完整赋值和 helper 命中。

CTest 26/26 通过。测试覆盖数字位编码、资源型旧值的类型切换、闭包捕获和 GC、保留赋值结果、独立中间入口、常量索引优先错误、临时目标及越界时的原始栈状态。

### 一元局部赋值对照

新增 `examples/baseline_unary_update_benchmark.js`，循环执行 `value = -value; flag = !flag; total = total + value;`。20 轮、每轮 5000 次；初始数字为 7，正负交替累计结果为 `0`。该样例同时覆盖数字取负和 Boolean 取反，不代表其它脚本的固定收益。修改前来自提交 `8e5477b`，保存为本机忽略的 `build-verify-release/unary-update-before-8e5477b.exe`。

Windows x64 合并 GetLocal + Negate/Not + SetLocal + Pop。数字取负直接翻转符号位，布尔取反直接交换 0/1；不创建临时槽，不同步栈深度。类型不匹配时仍按原始四步执行。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序；每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 13.674 ms | 6.195 ms | 2.207x | 74.158 ms | 75.841 ms |
| 2 | 13.652 ms | 6.597 ms | 2.069x | 72.579 ms | 74.658 ms |
| 3 | 13.944 ms | 8.174 ms | 1.706x | 74.757 ms | 77.196 ms |

三组 JIT on 中位数降低约 54.7%、51.7%、41.4%，第三组有更明显的波动。数据支持保留该专项合并，但不能将一次微基准的加速比推广到任意脚本。所有结果均为 `0`，baseline 入口计数为 19。

另行单轮开启采样，仅核对 helper 调用次数，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 95,000 | 0 |
| `SetLocal` | 95,000 | 0 |
| `PushNumberSlot` | 190,076 | 95,076 |
| `Pop` | 285,019 | 95,019 |
| `PushConstant` | 19 | 19 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

合计减少 475,000 次 helper 调用。修改前的一元操作本身已有栈顶快路径，这次消除的是局部读写与临时结果处理；循环条件、Boolean 初始化及回边调用仍保留。非命中路径不由此微基准计数，另由专项测试验证。

CTest 27/27 通过。测试覆盖数字位编码、Boolean 取反、同槽/不同槽、类型变化、truthy 与类型错误、捕获变量/GC、保留赋值结果、helper 计数、非法槽、临时槽边界和三个中间独立入口。

### 独立目标算术赋值对照

新增 `examples/baseline_arithmetic_assign_benchmark.js`，每轮分别执行 `result = i + offset;` 和 `result = i * 2;`，两次结果各自加入 total。20 轮、每轮 5000 次、offset=2，预期结果为 `750050000`，没有只累计后来覆盖的结果。修改前来自提交 `8afa636`，保存为本机忽略的 `build-verify-release/arithmetic-assign-before-8afa636.exe`。

Windows x64 把既有五条算术更新合并扩展到独立目标；Number 快路径不再构造结果槽或 Pop。支持四种算术与局部/数字常量右值，目标可以是左槽、右槽或第三个槽；旧目标不是 Number 时仍执行完整赋值。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序。每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 11.943 ms | 7.610 ms | 1.569x | 101.312 ms | 95.211 ms |
| 2 | 11.594 ms | 8.091 ms | 1.433x | 97.159 ms | 95.753 ms |
| 3 | 11.504 ms | 8.383 ms | 1.372x | 101.199 ms | 106.437 ms |

三组 JIT on 中位数降低约 36.3%、30.2%、27.1%。普通 VM 与 JIT 数据仍有系统噪声；这是独立目标赋值循环的专项收益，不是任意脚本的固定加速比。所有结果均为 `750050000`，baseline 入口计数为 19。

另行单轮开启采样，仅核对 helper 数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 285,095 | 95,095 |
| `Pop` | 285,019 | 95,019 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

两种赋值各减少临时结果压栈与末尾 Pop，合计减少 380,000 次 helper 调用。循环条件结果槽与回边调用仍保留；不将减少调用数解释为同等比例的全程序加速。

CTest 28/28 通过。测试覆盖四种算术、两种右值、三种目标、特殊数字、动态正负零、源值不变、类型变化、保留结果、捕获槽/GC、方法帧、独立中间入口、非法槽、临时结果槽和错误优先级。

### 比较赋值对照

新增 `examples/baseline_comparison_assign_benchmark.js`，循环执行 `flag = i < limit;` 和 `flag = i > 2500;`，每次结果分别用于 if 条件并累计 true 次数。20 轮、每轮 5000 次、limit=2500，预期结果为 `99980`。修改前来自提交 `2a0bb40`，保存为本机忽略的 `build-verify-release/comparison-assign-before-2a0bb40.exe`。

Windows x64 合并 Less/Greater/Equal 的五条比较赋值指令。数字源与已存在的 Boolean 目标命中快路径后，直接写布尔载荷，不构造结果槽；其它类型或源目标别名仍走原有比较和完整赋值。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序；每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 21.996 ms | 16.343 ms | 1.346x | 96.013 ms | 104.094 ms |
| 2 | 22.563 ms | 16.538 ms | 1.364x | 97.645 ms | 100.223 ms |
| 3 | 34.810 ms | 16.286 ms | 2.137x | 109.478 ms | 109.470 ms |

前两组 JIT on 中位数降低约 25.7%、26.7%。第三组降低约 53.2%，但修改前的 JIT 耗时明显高于前两组，原因未定位，不将该组作为典型收益。数据支持保留该专项合并，不能推广为任意脚本的固定加速比。所有结果均为 `99980`，baseline 入口计数为 19。

另行单轮开启采样，仅核对 helper 数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 190,000 | 190,000 |
| `SetLocal` | 190,000 | 0 |
| `PushNumberSlot` | 285,076 | 95,076 |
| `Pop` | 475,019 | 285,019 |
| `PushConstant` | 19 | 19 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

合计减少 570,000 次 helper 调用。两个 if 条件仍复制 flag 并弹出，因此 GetLocal 不变、Pop 没有清零；循环条件和回边调用也仍保留。

CTest 29/29 通过。测试覆盖三种比较、两种右值、特殊数字、false 成功状态、类型变化、各类相等 fallback、源目标别名、保留结果、捕获槽/GC、方法帧、非法槽、临时槽、错误优先级及四个中间独立入口。

### 局部条件分支对照

新增 `examples/baseline_local_branch_benchmark.js`，每次循环用 `if (flag)` 累计 true 次数，再用 `flag = !flag` 翻转条件。20 轮、每轮 5000 次，flag 初始为 true，预期结果为 `50000`。修改前来自提交 `ae579e9`，保存为本机忽略的 `build-verify-release/local-branch-before-ae579e9.exe`。

Windows x64 在真假两条路径都丢弃条件副本时，合并 GetLocal + JumpIfFalse + 真路径 Pop，假路径改跳到其 Pop 之后；原假路径 Pop 仍保留给其他入口。Boolean 直接读局部载荷，非 Boolean 使用不改栈的 LocalTruthy 三态 helper。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序；每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 12.989 ms | 7.868 ms | 1.651x | 83.379 ms | 90.138 ms |
| 2 | 12.886 ms | 11.922 ms | 1.081x | 96.518 ms | 82.788 ms |
| 3 | 13.739 ms | 7.768 ms | 1.769x | 79.306 ms | 81.476 ms |

三组 JIT on 中位数降低约 39.4%、7.5%、43.5%。第二组修改后数据明显高于其它组，普通 VM 也有波动，不将本微基准看成固定加速比。三组方向支持保留该合并；所有结果为 `50000`，baseline 入口计数为 19。

另行单轮开启采样，仅核对 helper 数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 95,000 | 0 |
| `Pop` | 190,019 | 95,019 |
| `PushConstant` | 19 | 19 |
| `PushNumberSlot` | 95,057 | 95,057 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |
| `LocalTruthy` | 0 | 0 |

Boolean 条件无需 helper，总计减少 190,000 次调用。循环的数字比较仍生成结果槽并 Pop，回边和初始化调用仍保留。非 Boolean 条件由专项测试验证一次 LocalTruthy fallback，不由该 Boolean 微基准计数。

CTest 30/30 通过。测试覆盖各类 truthiness、条件栈效果、短路结果及副作用、if/while、break/continue、捕获槽/GC、方法帧、非法槽、独立入口、共享假路径 Pop 及三态 ABI 的失败和完成状态。

### 直接局部比较分支对照

新增 `examples/baseline_comparison_branch_benchmark.js`，包含 `while (i < count)` 和 `i < midpoint`、`i > midpoint`、`i == midpoint` 三个 if。每次迭代恰好有一个 if 为真，20 轮、每轮 5000 次，预期结果为 `100000`。修改前来自提交 `28c8db1`，保存为本机忽略的 `build-verify-release/comparison-branch-before-28c8db1.exe`。

Windows x64 在真假路径均清理结果时，合并双局部读取、比较、JumpIfFalse 和真路径 Pop；数字直接比较并跳转，不构造 Boolean Value。非数字调用一次 CompareLocalsTruthy，内部仍完整执行原始比较和清理顺序。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序；每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 15.762 ms | 7.404 ms | 2.129x | 126.837 ms | 129.603 ms |
| 2 | 15.639 ms | 8.171 ms | 1.914x | 119.849 ms | 117.976 ms |
| 3 | 15.562 ms | 7.517 ms | 2.070x | 123.985 ms | 119.065 ms |

三组 JIT on 中位数降低约 53.0%、47.8%、51.7%。普通 VM 仍存在波动；这是同机局部比较循环的专项收益，不是任意脚本的固定加速比。所有结果为 `100000`，baseline 入口计数为 19。

另行单轮开启采样，仅核对 helper 数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 380,076 | 57 |
| `Pop` | 380,019 | 0 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |
| `CompareLocalsTruthy` | 0 | 0 |

四个局部比较条件每次分别省一个结果压栈和清理 Pop，连同 while 最后的假条件，合计少 760,038 次 helper 调用。剩余 57 次数字压栈用于初始化，回边与返回调用仍保留；不能把减少调用数等同于同比例的全程序提速。此 Number 微基准不命中非数字 fallback，另由专项测试确认一次合并 helper。

CTest 31/31 通过。测试覆盖特殊数字、完整类型 fallback、对象身份/GC、短路值及副作用、循环和方法帧、独立中间入口、共享假路径 Pop、临时右槽、非法源槽和 operation，以及成功/失败时的栈状态。

### 直接数字常量比较分支对照

新增 `examples/baseline_constant_branch_benchmark.js`，包含 `while (i < 5000)` 和 `i < 2500`、`i > 2500`、`i == 2500` 三个 if。每次迭代恰好有一个 if 为真，运行 20 轮，预期结果为 `100000`。修改前来自提交 `ebf401e`，保存为本机忽略的 `build-verify-release/constant-branch-before-ebf401e.exe`。

Windows x64 把直接比较分支扩展到数字常量右值，Number 左值直接和嵌入位编码比较，不创建结果槽。非数字左值通过一次 CompareLocalConstantTruthy，从当前函数的常量池读取并按原顺序完整比较、清理。两个 fallback 共用 VM 比较收尾逻辑。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序；每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 12.904 ms | 5.697 ms | 2.265x | 93.870 ms | 105.038 ms |
| 2 | 13.363 ms | 5.943 ms | 2.249x | 92.437 ms | 102.685 ms |
| 3 | 13.364 ms | 6.351 ms | 2.104x | 93.860 ms | 92.077 ms |

三组 JIT on 中位数降低约 55.9%、55.5%、52.5%。普通 VM 仍有系统波动，这是同机常量比较循环的专项收益，不是任意脚本的固定加速比。所有结果为 `100000`，baseline 入口计数为 19。

另行单轮开启采样，仅核对 helper 数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushNumberSlot` | 380,057 | 38 |
| `Pop` | 380,019 | 0 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |
| `CompareLocalConstantTruthy` | 0 | 0 |

四个常量比较条件连同 while 的最后假条件，合计少 760,038 次 helper 调用。剩余 38 次数字压栈用于初始化，回边和返回仍保留。此 Number 微基准不命中类型 fallback，另由专项测试确认一次合并 helper。

CTest 32/32 通过。测试覆盖特殊数字、三种比较、源值不变、完整类型 fallback、保留短路结果、循环和方法帧、独立中间入口、共享假路径 Pop、索引/operation 错误及失败栈状态。

### 布尔常量赋值对照

新增 `examples/baseline_boolean_assign_benchmark.js`，每轮依次执行 `flag = true`、`flag = false`，各自用 if 检查并累计。运行 20 轮、每轮 5000 次，只有 true 条件增加 total，预期结果为 `100000`。修改前来自提交 `83855a5`，保存为本机忽略的 `build-verify-release/boolean-assign-before-83855a5.exe`。

Windows x64 扩展 Constant + SetLocal + Pop 合并。Boolean 目标直接写入 0/1 载荷，其它旧类型通过完整 Value 赋值处理。Number 赋值继续使用原有 64 位写入路径。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换执行顺序；每次采样使用新 VM，包含 JIT 编译与前期冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 15.653 ms | 4.962 ms | 3.155x | 79.212 ms | 73.682 ms |
| 2 | 15.439 ms | 5.958 ms | 2.591x | 71.626 ms | 73.838 ms |
| 3 | 15.422 ms | 4.735 ms | 3.257x | 71.960 ms | 76.876 ms |

三组 JIT on 中位数降低约 68.3%、61.4%、69.3%。普通 VM 仍有波动，这是同机布尔赋值循环的专项收益，不是任意脚本的固定加速比。所有结果为 `100000`，baseline 入口计数为 19。

另行单轮开启采样，仅核对 helper 数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `PushConstant` | 190,019 | 19 |
| `SetLocal` | 190,000 | 0 |
| `Pop` | 190,000 | 0 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |
| `AssignConstant` | 0 | 0 |

合计减少 570,000 次 helper 调用。Boolean 初始化仍有 19 次压常量，数字初始化、回边和返回也仍保留。此微基准的目标均为 Boolean，不命中类型 fallback；专项测试另行确认完整赋值 helper。

CTest 33/33 通过。测试覆盖布尔真假、Number 路径回归、类型变化、资源型旧值、闭包捕获/GC、方法帧、保留结果、非法索引/目标、临时目标及独立中间入口。

### 布尔局部复制对照

新增 `examples/baseline_boolean_copy_benchmark.js`，每轮执行 `flag = yes` 和 `flag = no`，随后用 if 校验真假并累计。20 次函数调用、每次 5000 轮，结果为 `100000`。修改前来自提交 `65bdb9f`，本机对照二进制为忽略的 `build-verify-release/boolean-copy-before-65bdb9f.exe`。

Windows x64 扩展已有 GetLocal + SetLocal + Pop 合并：同类型 Boolean 直接复制一个字节载荷，Number 优先路径不变，其它组合仍使用完整 CopyLocal helper。没有修改 VM/helper ABI 或 bytecode 格式。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换前后版本顺序。每次采样使用新 VM，包含 JIT 编译和冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 10.343 ms | 5.166 ms | 2.002x | 74.482 ms | 81.228 ms |
| 2 | 10.408 ms | 4.978 ms | 2.091x | 75.534 ms | 77.454 ms |
| 3 | 10.372 ms | 5.066 ms | 2.047x | 72.061 ms | 72.535 ms |

三组 JIT on 中位数降低约 50.1%、52.2%、51.2%；所有结果为 `100000`，baseline 入口计数为 19。普通 VM 仍有系统波动，这是布尔复制循环的专项收益，不能外推为任意程序的固定加速比。

另行单轮开启 helper 采样，仅核对调用数量，不以采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `CopyLocal` | 190,000 | 0 |
| `PushConstant` | 57 | 57 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

合计减少 190,000 次 helper 调用。布尔/数字初始化、循环回边和返回仍保留；本微基准不会命中跨类型 fallback，专项测试另行验证该路径。

同样以 3 轮预热、15 轮采样核对原数字局部复制基准，结果均为 `249950000`，baseline 入口计数均为 19：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 3.822 ms | 4.061 ms | 55.408 ms | 65.945 ms |
| 2（交换顺序） | 4.029 ms | 3.742 ms | 68.660 ms | 56.904 ms |
| 3 | 3.817 ms | 3.751 ms | 56.910 ms | 60.350 ms |

数字复制 JIT on 三组方向不一致，没有测得稳定退化或额外收益；保留原 Number guard/复制指令，仅改变其失败分支的目标。更大负载下仍应继续观测代码体积与指令缓存影响。

CTest 34/34 通过。测试覆盖全部布尔组合、false 成功状态、数字逐位复制、类型变化与资源型旧目标、同槽自赋值、捕获/GC、方法帧、保留结果、非法源/目标、临时槽及独立 SetLocal/Pop 跳转入口。

### 布尔局部相等分支对照

新增 `examples/baseline_boolean_equality_benchmark.js`，每轮执行四个局部布尔相等条件，其中两个为真、两个为假，并覆盖同槽比较。20 次函数调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `cd9d982`，本机忽略的对照二进制为 `build-verify-release/boolean-equality-before-cd9d982.exe`。

Windows x64 的 GetLocal + GetLocal + Equal + JumpIfFalse + Pop 合并增加 Boolean 快路径。只比较两个一字节载荷，不构造操作数副本或结果槽；Number 路径优先，跨类型相等仍调用 CompareLocalsTruthy，Less/Greater、常量右值与保留结果的表达式不参与扩展。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序。每次采样使用新 VM，包含 JIT 编译与冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 58.831 ms | 8.019 ms | 7.336x | 122.883 ms | 119.269 ms |
| 2 | 62.159 ms | 7.772 ms | 7.998x | 120.671 ms | 124.342 ms |
| 3 | 58.131 ms | 9.436 ms | 6.160x | 119.221 ms | 126.988 ms |

三组 JIT on 中位数降低约 86.4%、87.5%、83.8%。所有结果为 `200000`，baseline 入口计数为 19。普通 VM 仍有波动，这是布尔相等循环的专项收益，不可外推为任意程序的固定加速比。

另行单轮开启 helper 采样，仅核对调用数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `CompareLocalsTruthy` | 380,000 | 0 |
| `PushConstant` | 57 | 57 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

减少 380,000 次 helper 调用，并消除 helper 内部对应的操作数复制和结果槽。初始化、回边和返回仍保留；本微基准不命中跨类型 fallback，专项测试另行确认三次完整比较 helper。

原数字比较分支基准使用同样的采样参数，结果均为 `100000`，baseline 入口计数均为 19：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 5.771 ms | 5.856 ms | 95.556 ms | 94.122 ms |
| 2（交换顺序） | 5.850 ms | 6.066 ms | 93.901 ms | 94.602 ms |
| 3 | 6.789 ms | 6.033 ms | 94.208 ms | 123.027 ms |

前两组数字 JIT on 略慢，第三组更快；未测得一致方向的退化或收益。第三组普通 VM 有明显波动，不能将单组差异归因于新快路径。原数字比较指令和真假值规则保持不变，代码体积/布局影响仍应在更大工作负载中继续观测。

CTest 35/35 通过。测试覆盖四种布尔组合、同槽比较、真假分支、跨类型相等和关系比较错误、数字/NaN 回归、源值保持、短路结果与副作用、while、方法帧、捕获/GC、临时右槽、错误栈状态、独立中间入口及共享假路径 Pop。

### 布尔局部相等赋值对照

新增 `examples/baseline_boolean_equality_assign_benchmark.js`，每轮执行四个 `flag = left == right`，并通过 if 累计两个真结果；覆盖同槽和不同槽比较。20 次函数调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `a061114`，本机忽略的对照二进制为 `build-verify-release/boolean-equality-assign-before-a061114.exe`。

Windows x64 扩展 GetLocal + GetLocal + Equal + SetLocal + Pop 合并。两源及旧目标均为 Boolean 时直接比较并写目标，支持目标与左/右源别名；其它情况仍按原顺序执行完整读取、比较和赋值。Number 路径优先，数字常量右值、Less/Greater 与保留结果的赋值不参与新路径。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序。每次采样使用新 VM，包含 JIT 编译与冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 68.573 ms | 10.765 ms | 6.370x | 155.159 ms | 163.654 ms |
| 2 | 70.656 ms | 10.901 ms | 6.482x | 160.719 ms | 163.030 ms |
| 3 | 68.764 ms | 11.985 ms | 5.738x | 154.025 ms | 160.184 ms |

三组 JIT on 中位数降低约 84.3%、84.6%、82.6%。所有结果为 `200000`，baseline 入口计数为 19。这是布尔相等赋值循环的专项收益，不能外推为任意程序的固定加速比；普通 VM 的变动也不能归因于未被执行的 native 路径。

另行单轮开启 helper 采样，仅核对调用数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 760,000 | 0 |
| `Equal` | 380,000 | 0 |
| `SetLocal` | 380,000 | 0 |
| `Pop` | 380,000 | 0 |
| `PushConstant` | 57 | 57 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

合计减少 1,900,000 次 helper 调用，初始化、回边与返回仍保留。本微基准不命中跨类型 fallback；专项测试另行确认 Number/Boolean 源覆盖字符串旧目标时完整 helper 顺序及数量。

原数字比较赋值基准使用相同采样参数，结果均为 `99980`，baseline 入口计数均为 19：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6.972 ms | 6.359 ms | 99.806 ms | 99.246 ms |
| 2（交换顺序） | 6.244 ms | 6.517 ms | 96.729 ms | 96.654 ms |
| 3 | 8.986 ms | 6.882 ms | 102.790 ms | 118.895 ms |

数字 JIT on 三组方向不一致，没有测得稳定退化或可归因的新收益。第三组波动较大，仍需在更大工作负载下观察代码体积和布局影响；原数字比较快路径指令与数值规则不变。

CTest 36/36 通过。测试覆盖四种布尔组合、旧目标真假、左/右/独立目标、同槽比较、源值保持、false 成功状态、数字/NaN 回归、资源型 fallback、保留结果、捕获/GC、方法帧、非法右源/目标、临时右槽/结果目标及四种中间独立入口。

### 布尔常量相等分支对照

新增 `examples/baseline_boolean_constant_branch_benchmark.js`，每轮检查四种局部布尔值与真假常量的相等组合，两个条件为真、两个为假。20 次函数调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `28ed88a`，本机忽略的对照二进制为 `build-verify-release/boolean-constant-branch-before-28ed88a.exe`。

Windows x64 扩展 GetLocal + Boolean Constant + Equal + JumpIfFalse + Pop 合并，常量直接作为一个字节立即数。左值通过 Boolean guard 后不压常量，不创建操作数副本或结果槽；跨类型回退调用已有 CompareLocalConstantTruthy，保留完整 Value 相等规则。Boolean 关系比较和保留短路结果的表达式不参与合并。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序；每次采样使用新 VM，包含 JIT 编译与冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 64.292 ms | 7.759 ms | 8.286x | 140.072 ms | 135.138 ms |
| 2 | 65.083 ms | 7.687 ms | 8.467x | 146.963 ms | 135.904 ms |
| 3 | 62.956 ms | 8.296 ms | 7.589x | 138.549 ms | 130.564 ms |

三组 JIT on 中位数降低约 87.9%、88.2%、86.8%。所有结果为 `200000`，baseline 入口计数为 19。这是布尔常量条件循环的专项收益，不代表任意程序的固定加速比。普通 VM 的波动不能归因于未执行的 native 快路径。

另行单轮开启 helper 采样，只核对调用数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 380,000 | 0 |
| `PushConstant` | 380,038 | 38 |
| `Equal` | 380,000 | 0 |
| `Pop` | 380,000 | 0 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

合计减少 1,520,000 次 helper 调用。初始化、回边和返回仍保留；本微基准不命中跨类型 fallback，专项测试另行验证十二次 CompareLocalConstantTruthy 调用。

原数字常量比较分支基准使用相同采样参数，结果均为 `100000`，baseline 入口计数均为 19：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6.563 ms | 6.368 ms | 99.834 ms | 102.932 ms |
| 2（交换顺序） | 6.227 ms | 6.813 ms | 101.906 ms | 105.120 ms |
| 3 | 6.342 ms | 6.403 ms | 103.065 ms | 105.531 ms |

数字 JIT on 第一组略快，后两组较慢，没有一致方向的结论；第二组差异约 9.4%，仍需继续观察，不能据此宣称数字路径完全没有性能影响。数字比较指令和数值语义不变，编译匹配分支与代码布局变化可能影响采样结果。

CTest 37/37 通过。测试覆盖真假常量和全部组合、数字/NaN 路径回归、跨类型相等、关系比较错误、短路值/副作用、while、方法帧、捕获/GC、非法左槽、常量索引错误及失败栈快照、独立中间入口和共享假路径 Pop。

### 布尔常量相等赋值对照

新增 `examples/baseline_boolean_constant_assign_benchmark.js`，每轮执行四种局部布尔值与真假常量的相等赋值，再用 if 累计两个真结果。20 次函数调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `333f54a`，本机忽略的对照二进制为 `build-verify-release/boolean-constant-assign-before-333f54a.exe`。

Windows x64 合并 GetLocal + Boolean Constant + Equal + SetLocal + Pop，源/目标均为 Boolean 时直接比较并写一个字节，支持同槽更新。类型不匹配时仍完整读取、比较和赋值；保留结果的表达式和其它常量类型不参与新路径。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序。每次采样使用新 VM，包含 JIT 编译和冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 82.903 ms | 10.227 ms | 8.106x | 162.850 ms | 159.194 ms |
| 2 | 74.446 ms | 9.988 ms | 7.454x | 156.150 ms | 161.903 ms |
| 3 | 85.575 ms | 18.093 ms | 4.730x | 207.056 ms | 219.480 ms |

三组 JIT on 中位数降低约 87.7%、86.6%、78.9%。所有结果为 `200000`，baseline 入口计数为 19。第三组普通 VM 和 JIT 耗时均偏高，保留该组数据但不把其差异当作精确的固定加速比；这是专项循环收益，不代表任意程序的提速。

另行单轮开启 helper 采样，仅核对调用数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 380,000 | 0 |
| `PushConstant` | 380,057 | 57 |
| `Equal` | 380,000 | 0 |
| `SetLocal` | 380,000 | 0 |
| `Pop` | 380,000 | 0 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

减少 1,900,000 次 helper 调用。初始化、回边和返回仍保留；本微基准不命中类型 fallback，专项测试另行确认每次资源型旧目标赋值均执行六类 helper、各一次（含 ReturnLocal）。

原数字比较赋值基准使用相同采样参数，结果均为 `99980`，baseline 入口计数均为 19：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6.612 ms | 6.435 ms | 118.076 ms | 127.145 ms |
| 2（交换顺序） | 12.455 ms | 7.710 ms | 124.669 ms | 110.056 ms |
| 3 | 10.320 ms | 6.117 ms | 126.360 ms | 144.985 ms |

三组未测得数字路径退化，但前版本及普通 VM 的波动很大，不能把数字基准的差值归因于 Boolean 优化。原数字快路径指令和语义保持不变，仍需在稳定环境、更大工作负载下观察代码布局影响。

CTest 38/38 通过。测试覆盖真假常量与旧目标真假、同槽更新、源值保持、false 成功状态、资源型目标及跨类型源 fallback、捕获/GC、方法帧、保留结果、非法源/目标的错误顺序、临时结果槽、四种独立中间入口及数字路径回归。

### 保留结果的布尔相等表达式对照

新增 `examples/baseline_boolean_expression_benchmark.js`，每轮用四个 let 声明保留四种布尔相等结果，再用 if 累计两个真结果。20 次调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `eb1c74d`，本机忽略的对照二进制为 `build-verify-release/boolean-expression-before-eb1c74d.exe`。

Windows x64 的 GetLocal + GetLocal + Equal 表达式合并增加 Boolean 快路径：只构造一个完整结果槽，不复制两源。复用 PushNumberSlot 构造无资源占位 Value，helper 返回后重新定位源地址，再写 Boolean 载荷和类型。不是完全无栈槽；数字运算仍优先原路径。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序。每次采样使用新 VM，包含 JIT 编译及冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 64.084 ms | 16.448 ms | 3.896x | 140.191 ms | 147.441 ms |
| 2 | 66.283 ms | 17.620 ms | 3.762x | 164.492 ms | 153.988 ms |
| 3 | 88.806 ms | 16.652 ms | 5.333x | 154.804 ms | 168.437 ms |

三组 JIT on 中位数降低约 74.3%、73.4%、81.2%；结果均为 `200000`，baseline 入口计数为 19。第三组前版本耗时偏高，保留原数据但不将其作为固定加速比；这是专项循环收益，不能外推到任意程序。

另行单轮开启 helper 采样，仅核对数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 760,000 | 0 |
| `Equal` | 380,000 | 0 |
| `PushNumberSlot` | 38 | 380,038 |
| `Pop` | 380,000 | 380,000 |
| `PushConstant` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

净减少 760,000 次 helper 调用。380,000 个比较结果必须构造，作用域结束时的 Pop 也仍保留。占位 helper 不新增 VM GC 对象；扩容边界由专项测试验证。

原数字局部表达式样例只执行加法、不进入新 Boolean 分支；同样参数的初测结果均为 `250150000`，入口计数均为 19：

| 初测组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 7.569 ms | 7.650 ms | 63.063 ms | 65.783 ms |
| 2（交换顺序） | 7.370 ms | 7.959 ms | 65.775 ms | 65.104 ms |
| 3 | 7.711 ms | 8.269 ms | 61.688 ms | 63.906 ms |

初测三组均偏慢，约 +1.1%、+8.0%、+7.2%，不能忽略。追加每版本 5 轮预热、31 轮正式采样，第一/三组先运行修改后版本、第二组先运行修改前版本，得到：

| 复核组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 7.171 ms | 7.122 ms | 57.800 ms | 61.488 ms |
| 2 | 7.308 ms | 7.583 ms | 59.236 ms | 63.217 ms |
| 3 | 7.591 ms | 7.696 ms | 61.485 ms | 59.837 ms |

复核差异缩小为约 -0.7%、+3.8%、+1.4%。没有确认初测 7%-8% 的稳定退化，但仍不能宣称数字路径完全无性能影响；需继续观察二进制布局和系统波动。保留全部初测/复核数据，未挑选较好结果替换。

CTest 39/39 通过。覆盖真假组合、同源槽、完整 Boolean 结果、源值保持、返回/声明/短路值、捕获/GC、方法帧、跨类型 fallback、80 个结果的扩容、临时右槽、非法源槽、独立读取/Equal 入口，以及原数字特殊值与错误回归。

### 保留结果的布尔常量相等表达式对照

新增 `examples/baseline_boolean_constant_expression_benchmark.js`，每轮用四个 let 声明保留局部值与真假常量的四种相等结果，再累计两个真值。20 次调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `ced1571`，本机忽略的对照二进制为 `build-verify-release/boolean-constant-expression-before-ced1571.exe`。

Windows x64 合并 GetLocal + Boolean Constant + Equal，只构造一个结果槽，不压源副本或常量。复用 PushNumberSlot 构造无资源占位 Value，扩容后重新定位源，再写 Boolean 载荷与类型。专门的赋值/直接分支优先，跨类型源仍完整读取、压常量和比较。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序；每次采样使用新 VM，包含 JIT 编译和冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 59.809 ms | 15.432 ms | 3.876x | 143.321 ms | 134.755 ms |
| 2 | 59.269 ms | 16.889 ms | 3.509x | 139.537 ms | 136.821 ms |
| 3 | 60.109 ms | 16.366 ms | 3.673x | 147.141 ms | 140.038 ms |

三组 JIT on 中位数降低约 74.2%、71.5%、72.8%。所有结果为 `200000`，baseline 入口计数为 19。这是布尔常量表达式专项循环收益，不代表任意程序的固定加速比；普通 VM 仍存在系统波动。

另行单轮开启 helper 采样，仅核对调用数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 380,000 | 0 |
| `PushConstant` | 380,038 | 38 |
| `Equal` | 380,000 | 0 |
| `PushNumberSlot` | 38 | 380,038 |
| `Pop` | 380,000 | 380,000 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

净减少 760,000 次 helper 调用。结果构造和作用域清理仍保留，不能说“完全不用栈”；初始化、回边及返回也保留。专项测试另行确认每次跨类型回退执行 GetLocal、PushConstant、Equal、Return，各一次。

原数字局部表达式样例只执行加法，不进入新 Boolean 常量路径；每版本改用 5 轮预热、31 轮正式采样，第二组交换顺序。结果均为 `250150000`、baseline 入口均为 19：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 8.615 ms | 7.138 ms | 59.678 ms | 59.020 ms |
| 2 | 9.612 ms | 8.325 ms | 63.027 ms | 58.011 ms |
| 3 | 8.944 ms | 7.614 ms | 64.301 ms | 66.285 ms |

三组未测得数字路径退化，但这些差值不能归因于未执行的 Boolean 快路径。数字 codegen 语义和核心指令不变；二进制布局、编译匹配与系统波动仍需继续观察，不将此表作为额外的数字优化收益。

CTest 40/40 通过。覆盖真假常量与全部源组合、完整结果类型、源值保持、返回/声明/短路结果、捕获/GC、方法帧、跨类型 fallback/helper 计数、80 个结果的扩容、非法源槽和独立 Constant/Equal 入口，现有数字特殊值及错误测试继续通过。

### Null/Undefined 常量相等分支对照

新增 `examples/baseline_empty_constant_branch_benchmark.js`，每轮判断 null/undefined 两个源与两个常量的四种相等条件，其中两个为真。20 次函数调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `ecbe0d3`，本机忽略的对照二进制为 `build-verify-release/empty-constant-branch-before-ecbe0d3.exe`。

Windows x64 合并 GetLocal + Null/Undefined Constant + Equal + JumpIfFalse + Pop。有效槽只比较类型标签，类型不匹配直接为 false，不需要类型 fallback；非法槽仍通过原 helper 产生错误。保留项目原有严格类型相等：null 不等于 undefined，也不等于其它假值。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序；每次采样使用新 VM，包含 JIT 编译和冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 56.928 ms | 7.067 ms | 8.055x | 121.680 ms | 130.868 ms |
| 2 | 62.211 ms | 8.999 ms | 6.913x | 187.192 ms | 119.525 ms |
| 3 | 59.611 ms | 7.317 ms | 8.147x | 120.665 ms | 128.927 ms |

三组 JIT on 中位数降低约 87.6%、85.5%、87.7%；结果均为 `200000`，baseline 入口计数为 19。第二组普通 VM 波动明显，保留原数据；本表只代表专项条件循环，不是任意程序的固定加速比。

另行单轮开启 helper 采样，仅核对调用数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 380,000 | 0 |
| `PushConstant` | 380,038 | 38 |
| `Equal` | 380,000 | 0 |
| `Pop` | 380,000 | 0 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

减少 1,520,000 次 helper 调用；初始化、回边与返回仍保留。类型不匹配不调用 CompareLocalConstantTruthy，专项测试确认有效 Number/Boolean/字符串/数组/对象等类型同样只使用返回 helper。

原数字常量比较分支使用每版本 5 轮预热、31 轮正式采样，第二组交换顺序。结果均为 `100000`、baseline 入口均为 19：

| 初测组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6.013 ms | 5.844 ms | 92.345 ms | 91.063 ms |
| 2 | 6.318 ms | 8.212 ms | 102.843 ms | 93.736 ms |
| 3 | 6.340 ms | 6.326 ms | 96.281 ms | 93.682 ms |

第二组 JIT on 约慢 30%，不能直接忽略。保持相同参数追加三组，第一/三组先运行修改后版本、第二组先运行修改前版本：

| 复核组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 6.222 ms | 5.993 ms | 97.799 ms | 97.729 ms |
| 2 | 6.509 ms | 6.177 ms | 97.290 ms | 100.324 ms |
| 3 | 9.624 ms | 8.675 ms | 138.458 ms | 106.212 ms |

复核未重现初测的单组退化，第三组前后版本和普通 VM 均明显变慢。不能确认稳定的数字路径退化，也不能把复核较快结果归因于未执行的新 Null/Undefined 快路径；仍需观察二进制布局及系统波动。保留全部初测与复核数据。

CTest 41/41 通过。覆盖相同/交叉空值类型、其它假值和资源类型、NaN、源值保持、捕获/GC、方法帧、短路值/副作用、while、非法源槽、四种中间独立入口、共享假路径 Pop 和原数字/布尔路径回归。

### Null/Undefined 常量相等赋值对照

新增 `examples/baseline_empty_constant_assign_benchmark.js`，每轮将四种空值常量比较结果赋给 flag，再累计两个真值。20 次调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `e2ab0e0`，本机忽略的对照二进制为 `build-verify-release/empty-constant-assign-before-e2ab0e0.exe`。

Boolean/Null/Undefined 常量共用比较赋值 emitter 和原完整 fallback。空值常量只比较有效源槽类型标签，目标已是 Boolean 时直接写结果；旧目标类型不同仍完整赋值。允许同槽更新，不改变 null/undefined 的严格类型相等语义。

关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序。每次采样使用新 VM，包含 JIT 编译与冷调用：

| 对照组 | 修改前 JIT on | 修改后 JIT on | 前 / 后 | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 82.978 ms | 11.431 ms | 7.259x | 170.623 ms | 160.107 ms |
| 2 | 99.295 ms | 10.588 ms | 9.378x | 229.179 ms | 163.872 ms |
| 3 | 75.105 ms | 9.683 ms | 7.756x | 175.729 ms | 168.622 ms |

三组 JIT on 中位数降低约 86.2%、89.3%、87.1%。结果均为 `200000`，baseline 入口计数为 19。第二组前版本/普通 VM 偏慢，保留原数据；这是专项循环收益，不代表任意程序的固定加速比。

另行单轮开启 helper 采样，仅核对调用数量，不使用采样耗时计算收益：

| helper | 修改前调用 | 修改后调用 |
| --- | ---: | ---: |
| `GetLocal` | 380,000 | 0 |
| `PushConstant` | 380,057 | 57 |
| `Equal` | 380,000 | 0 |
| `SetLocal` | 380,000 | 0 |
| `Pop` | 380,000 | 0 |
| `PushNumberSlot` | 38 | 38 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

减少 1,900,000 次 helper 调用，初始化、回边和返回仍保留。源类型不匹配直接为 false；资源型旧目标回退由专项测试另行确认六类 helper 各一次（含 ReturnLocal）。

由于复用了原布尔常量赋值 emitter，另测 `baseline_boolean_constant_assign_benchmark.js`，每版本 5 轮预热、31 轮采样，第二组交换顺序；结果均为 `200000`、入口均为 19：

| 初测组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 9.839 ms | 10.238 ms | 168.978 ms | 174.345 ms |
| 2 | 10.192 ms | 10.165 ms | 170.080 ms | 171.245 ms |
| 3 | 10.421 ms | 11.334 ms | 170.171 ms | 168.432 ms |

第三组约慢 8.8%，不能直接忽略。保持采样参数追加三组，第一/三组先运行修改后版本、第二组先运行修改前版本：

| 复核组 | 修改前 JIT on | 修改后 JIT on | 修改前 JIT off | 修改后 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 10.258 ms | 10.350 ms | 165.409 ms | 165.673 ms |
| 2 | 11.553 ms | 10.622 ms | 172.560 ms | 169.607 ms |
| 3 | 14.386 ms | 11.897 ms | 175.782 ms | 169.363 ms |

复核未重现初测单组退化，方向不一致；不能确认稳定的布尔路径退化或额外收益，也不能宣称完全没有性能影响。布尔载荷比较和 guard 逻辑保持原样，仍需观察二进制布局与系统波动。保留全部初测/复核数据。

CTest 42/42 通过。覆盖同/交叉空值类型、其它有效源类型、旧目标真假、同槽更新、资源型回退/helper 数量、源对象身份、保留结果、捕获/GC、方法帧、非法源/目标优先级、临时结果槽、四种独立中间入口和原布尔/数字回归。新增对象断言直接检查引用身份与结果，不依赖对象显示格式。

### 保留结果的 Null/Undefined 相等表达式对照

新增 `examples/baseline_empty_constant_expression_benchmark.js`，每轮用四个 let 保留空值相等结果，再累计两个真值。20 次调用、每次 5000 轮，结果为 `200000`。修改前来自提交 `c8038f5`，本机对照二进制为忽略的 `build-verify-release/empty-constant-expression-before-c8038f5.exe`。

最终实现单独使用 EmptyConstantComparisonExpression，保留原 Boolean emitter。有效源槽比较类型标签，再将完整占位槽写为 Boolean；不压源副本或常量。结果槽与作用域 Pop 仍必要，净栈变化为 +1，不改变 null/undefined 类型相等语义。

最终独立版本关闭 helper 采样，每组每版本 3 轮预热、15 轮正式采样，第二组交换顺序。每次采样使用新 VM，包含 JIT 编译与冷调用：

| 对照组 | 修改前 JIT on | 最终 JIT on | 前 / 后 | 修改前 JIT off | 最终 JIT off |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 58.800 ms | 15.082 ms | 3.899x | 147.696 ms | 146.181 ms |
| 2 | 59.428 ms | 15.211 ms | 3.907x | 141.695 ms | 151.231 ms |
| 3 | 63.822 ms | 15.008 ms | 4.253x | 143.323 ms | 144.577 ms |

三组 JIT on 中位数降低约 74.4%、74.4%、76.5%；结果均为 `200000`，baseline 入口均为 19。这是专项循环收益，不能外推为任意程序的固定加速比。

另行单轮开启 helper 采样，仅核对调用数量，不使用采样耗时计算收益。前版本采样来自同一对照二进制和未变的样例，最终独立版本重新核对：

| helper | 修改前调用 | 最终调用 |
| --- | ---: | ---: |
| `GetLocal` | 380,000 | 0 |
| `PushConstant` | 380,038 | 38 |
| `Equal` | 380,000 | 0 |
| `PushNumberSlot` | 38 | 380,038 |
| `Pop` | 380,000 | 380,000 |
| `RecordLoopBackedge` | 95,000 | 95,000 |
| `ReturnLocal` | 19 | 19 |

净减少 760,000 次 helper 调用。占位构造和作用域清理仍保留，初始化、回边与返回也保留。有效源类型不匹配直接为 false，不进入类型 fallback。

最初尝试共享 Boolean/Null/Undefined emitter，虽语义测试通过，但原布尔表达式复核出现持续较慢信号，未保留。候选二进制保存为忽略的 `build-verify-release/empty-expression-shared-candidate.exe`。其空值样例（3 轮预热、15 轮采样）原始数据如下，不用于计算最终收益：

| 共享候选组 | 修改前 JIT on | 候选 JIT on | 修改前 JIT off | 候选 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 62.662 ms | 15.129 ms | 146.047 ms | 184.609 ms |
| 2 | 75.412 ms | 15.248 ms | 148.444 ms | 161.963 ms |
| 3 | 62.207 ms | 14.767 ms | 159.335 ms | 151.155 ms |

共享候选的 `baseline_boolean_constant_expression_benchmark.js` 每版本 5 轮预热、31 轮正式采样，初测第二组交换顺序；复核第一/三组先候选、第二组先前版本。结果均为 `200000`、入口均为 19：

| 阶段/组 | 修改前 JIT on | 共享候选 JIT on | 修改前 JIT off | 候选 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 初测 1 | 16.386 ms | 15.818 ms | 138.781 ms | 150.815 ms |
| 初测 2 | 16.008 ms | 16.041 ms | 143.147 ms | 144.536 ms |
| 初测 3 | 17.409 ms | 19.777 ms | 146.068 ms | 150.169 ms |
| 复核 1 | 15.266 ms | 17.261 ms | 136.607 ms | 145.640 ms |
| 复核 2 | 15.385 ms | 19.153 ms | 149.440 ms | 139.368 ms |
| 复核 3 | 22.414 ms | 28.809 ms | 241.104 ms | 212.957 ms |

初测第三组约慢 13.6%，复核三组约慢 13.1%、24.5%、28.5%，不能仅归为单组噪声。因此恢复原 Boolean emitter，空值表达式独立发射；这不是对具体退化根因的证明。

最终独立版本用相同布尔样例及 5/31 参数复测，第二组交换顺序，结果和入口计数不变：

| 独立版本组 | 修改前 JIT on | 最终 JIT on | 修改前 JIT off | 最终 JIT off |
| --- | ---: | ---: | ---: | ---: |
| 1 | 14.468 ms | 15.281 ms | 142.409 ms | 143.518 ms |
| 2 | 17.566 ms | 16.697 ms | 142.325 ms | 145.710 ms |
| 3 | 16.727 ms | 17.066 ms | 144.462 ms | 155.724 ms |

最终差异约 +5.6%、-4.9%、+2.0%，未再出现复核的持续较慢信号，但不能宣称原布尔路径完全无性能影响。保留所有候选和最终测量，不混用候选收益作为最终结果；二进制布局和环境波动仍需观察。

最终独立版本重新通过 CTest 43/43。覆盖同/交叉空值类型、其它有效源类型、完整 Boolean 结果、源对象身份、声明/返回/短路值、捕获/GC、方法帧、80 个结果及对象源的扩容、非法源槽、独立 Constant/Equal 入口，以及现有布尔/数字回归。
