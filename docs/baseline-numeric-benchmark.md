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

代码中 `GetLocal`、`SetLocal` 和栈操作仍调用完整 runtime helper；算术与比较快路径也仍需要栈深度同步。下一步可以从 Windows x64 的 Number-only `GetLocal` / `SetLocal` 快路径开始，保持容量检查、GC 根和栈快照契约，使用本脚本进行修改前后对照。

局部变量 helper 是从代码得出的优化候选，尚未通过 profiler 确认为最大热点。实施时应记录前后 Release 数据，确认减少 helper 调用确实带来收益。
