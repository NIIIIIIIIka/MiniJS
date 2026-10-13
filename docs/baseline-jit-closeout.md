# Baseline JIT 阶段收口

验收日期：2026-10-13。状态：Windows x64 当前实现验收通过，进入功能冻结与维护；不等同于所有平台的发布认证。

## 冻结范围

- 已完成 bytecode 校验/解码、热度调度、decoded/native 入口和 Runtime ABI。
- 保留现有数字、Boolean、Null/Undefined、局部槽、赋值、比较、分支和返回快路径。
- 对象/属性、数组、闭包/upvalue、类/继承/方法、普通调用和 super 调用保留完整 runtime helper；不要求所有 opcode 都内联。
- 真实 VM 栈仍是 GC 根；helper 后必须刷新快照、重新定位地址。窄返回值统一零扩展后再判断状态。
- 现有 offset 映射仅定位指令/合并单元，不提供独立 deopt 恢复点。

## 本次修复

独立 Release 全套测试发现此前未优化构建未暴露的 ABI 缺口：bool/uint8_t helper 只保证返回寄存器低字节有效，机器码却直接判断整个 EAX，可能误报 baseline entry did not complete。

Windows x64 每次 helper 调用后追加 movzx eax,al；ARM64 追加 uxtb w0,w0。原 helper 原型、0/1 成功协议和 0/1/2 三态协议不变。新增生成字节断言，现有 truthy、比较回退和异常测试在 Release 下真实执行。此次不是新增快路径。

## 验收结果

工具链：Windows x64、GNU MinGW 13.1.0、Ninja。常规构建使用原 build-ninja；独立 Release 使用 -O3 -DNDEBUG，开启 MINIJS_BUILD_TESTS；生产 Release 使用原 build-verify-release，测试钩子关闭。

| 项目 | 结果 |
| --- | --- |
| 常规构建 CTest | 43/43 通过 |
| 独立 Release CTest | 43/43 通过 |
| 生产 Release JIT on/off 跨功能样例 | 11/11 结果一致，无 JIT 编译失败 |
| IC on/off 对照 | inline_cache_benchmark 结果均为 10000 |
| MinGW DLL 配套 | 三个 DLL 哈希与编译器运行时一致 |
| 去除 MinGW/MSYS PATH 后启动 | method_calls 结果为 10，启动成功 |

11 个样例包括 closure_counter、objects_arrays、method_calls、super_calls、polymorphic_method_calls、polymorphic_static_calls、polymorphic_property_reads、polymorphic_property_writes，以及 numeric、Boolean constant expression、empty constant expression 基准。

闭包实际进入 baseline 2 次，方法 4 次、多态方法/静态方法各 6 次、属性读写各 3 次，三个循环基准各 19 次。objects_arrays 没有函数入口，super_calls 只有冷调用且入口计数为 0；不以生成 native code 冒充实际执行覆盖。super 的 native 路径由完整单元测试覆盖。

本次 CLI 比较只做单轮结果一致性验收，不据此报告新性能收益。历史专项数据、候选回滚与波动见 [性能报告](baseline-numeric-benchmark.md)；旧数据对应各自提交，不能直接作为 ABI 修复后的统一性能基线。

## 重复验收

```powershell
cmake --build build-ninja
ctest --test-dir build-ninja --output-on-failure
cmake -S . -B build-closeout-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMINIJS_BUILD_TESTS=ON
cmake --build build-closeout-release
ctest --test-dir build-closeout-release --output-on-failure
cmake --build build-verify-release
.\build-verify-release\minijs.exe --benchmark-compare-jit examples\method_calls.js --benchmark-samples 1 --benchmark-warmup 0 --benchmark-format json
.\build-verify-release\minijs.exe --benchmark-compare examples\inline_cache_benchmark.js --benchmark-samples 1 --benchmark-warmup 0 --benchmark-format json
```

新目录首次配置须使用同一工具链，可显式指定 CMAKE_CXX_COMPILER/CMAKE_MAKE_PROGRAM；已有目录不要混用编译器。--benchmark-compare 比较 IC 开关，--benchmark-compare-jit 比较 JIT 开关，不是一次性比较 Interpreter/VM/IC/JIT 四层。样例有 print 时，stdout 会在 JSON 前包含脚本输出。

## 停止线与风险

- 本阶段不再增加 opcode 内联、比较组合、语法或更多微基准。复杂 runtime helper 留在原边界。
- 仅接受正确性、ABI/GC 安全、可复现性能回归、构建和文档修复；变更后重复两种构建验收。
- ARM64 有后端和按平台条件编译的回归测试，但本机未做 ARM64 编译/实机运行；Windows x64 验收不能替代它。
- 当前并非完整 ECMAScript 实现，Mark-Sweep GC 与外部 shared_ptr 兼容层并存。
- 性能波动及二进制布局影响仍未完全消除；不能承诺所有脚本提速。
- 不实现 OSR、deopt、safepoint 恢复、优化 IR 或 shape 专门化。Optimize JIT、外部 VM 对比另行立项，不由普通“下一步”自动启动。
- 本次不改项目版本、不自动打 tag、不发布。独立规划文档和原暂存内容保持原状。

后续进入新阶段前，先确定一个有界目标与验收标准；当前 Baseline 功能扩展在此停止。
