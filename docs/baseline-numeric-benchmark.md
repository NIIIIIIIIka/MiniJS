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
