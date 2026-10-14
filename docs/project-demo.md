# 项目演示与结果解读

本指南只使用已有功能和样例，不增加引擎功能。目标是演示 Source -> AST / Bytecode -> VM -> IC / Baseline 的关系，而不是开展新一轮性能优化。Windows x64 已验证；ARM64 验收边界见 [收口记录](baseline-jit-closeout.md)。

## 1. 准备程序

在仓库根目录打开 PowerShell，使用现有 Release 构建：

```powershell
cmake --build build-verify-release
$exe = '.\build-verify-release\minijs.exe'
```

没有这个构建目录时，先配置 Ninja 和编译器的 PATH，再执行：

```powershell
cmake -S . -B build-verify-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINIJS_BUILD_TESTS=OFF
cmake --build build-verify-release
$exe = '.\build-verify-release\minijs.exe'
```

不要在已有构建目录中切换工具链。性能对照使用 Release，避免同时运行构建或其它负载；启动时若出现 DLL 入口错误，先核对同目录 MinGW DLL 与编译工具链是否配套。

## 2. 同一程序的两种执行方式

```powershell
& $exe --interp examples/method_calls.js
& $exe examples/method_calls.js
```

两条命令都应输出 `10`。第一条直接执行 AST，第二条使用默认 Bytecode VM。样例包含类、构造方法、实例属性和方法调用：相同源码可以经过不同执行层得到一致结果。

这不是性能对照，也不能据此推断冷函数已进入 native baseline。

## 3. 观察闭包与字节码

```powershell
& $exe --bytecode examples/closure_counter.js
& $exe examples/closure_counter.js
```

第一条显示顶层 chunk，能看到 `OP_CLOSURE`、`OP_DEFINE_GLOBAL`、`OP_GET_GLOBAL` 和 `OP_CALL`。这里展示的是顶层指令，不是完整嵌套函数反汇编，也不是机器码。

第二条依次输出 `1`、`2`、`3`。`makeCounter` 返回后，`next` 仍能访问并修改捕获的 `count`。可以结合 [闭包设计](bytecode-closure.md) 讲解 upvalue，而不必从机器码开始。

## 4. 单独对照 Inline Cache

```powershell
& $exe --benchmark-compare examples/inline_cache_benchmark.js --benchmark-samples 3 --benchmark-warmup 1 --benchmark-format json
```

验收重点：

- `result` 为 `"10000"`；CLI 会检查 IC 开关两边的返回值一致。
- `with_ic.inline_cache.get_property.hits` 大于零，证明属性缓存产生了命中。
- `without_ic.inline_cache.get_property.hits` 为零。

`--benchmark-compare` 对照 IC，不是 JIT。命中并不保证这个小样例提速；计时受执行成本、机器负载和测量波动影响，不能只凭一次 `speedup` 宣称优化有效。

## 5. 单独对照 Baseline JIT

```powershell
& $exe --benchmark-compare-jit examples/baseline_numeric_benchmark.js --benchmark-samples 3 --benchmark-warmup 1 --benchmark-format json
```

验收重点：

- `result` 为 `"125025000"`；CLI 会检查 JIT 开关两边的返回值一致。
- `without_jit.jit.counters.baseline_entries` 为零。
- `with_jit.jit.functions.failed` 为零，`with_jit.jit.counters.baseline_entries` 大于零。
- Windows x64 上 `with_jit.jit.code.native_entries` 大于零，表明存在已生成的 native 入口。

注意：`code.native_entries` 是具有 native 入口的函数数量，不是执行次数；`counters.baseline_entries` 才是进入 baseline 的计数。不能把生成代码数当成 native 实际执行次数，也不能把总 baseline 次数无条件全部归为 native。

复杂操作仍可调用 runtime helper；进入 native baseline 不代表每个 opcode 都完全内联。边界见 [Baseline 设计](baseline-jit.md)。

## 6. 正确解读采样

- `samples=3` 表示三轮正式测量，`warmup=1` 表示额外一轮不计入计时的预热。
- 每轮重新编译字节码并创建 VM，预热不保留上一轮 VM 的 JIT 代码；它不是持续复用同一个热 VM 的吞吐测试。
- `compile_us` 是字节码编译耗时，不含 Lexer / Parser，也不单独表示 native 编译耗时。执行中发生的 JIT 编译包含在 `run_us` 中。
- 两项计时各自取正式样本中位数；IC/JIT 计数来自最后一轮，不是三轮求和，也不一定对应中位数那轮。
- benchmark 使用专门的热度阈值，不能直接推断默认运行模式下相同样例的 tiering 时机。
- 使用无 `print` 的两个基准读取纯 JSON；其它样例的脚本输出可能位于 JSON 之前。

本指南不固定速度比，不承诺所有程序提速，不做外部 JS 引擎排名。历史专项数据见 [性能报告](baseline-numeric-benchmark.md)。

## 完成标准

四组演示命令成功、预期结果一致、IC 命中和 baseline 进入证据可解释，即完成本次展示整理。完整正确性验收仍以 [收口记录](baseline-jit-closeout.md) 中的两种构建测试为准，演示不能替代测试。

至此停止新增 Baseline 功能。Optimize JIT、外部 VM 对比和 ARM64 实机验证均需另定目标与验收标准；本指南不自动启动它们，也不创建版本 tag 或发布。
