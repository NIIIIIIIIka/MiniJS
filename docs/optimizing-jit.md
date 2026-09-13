# Optimize JIT 设计

本文档描述 MiniJS 的第二层优化 JIT 设计。它不是当前马上实现的阶段，而是在 Baseline JIT 稳定之后，利用 `FeedbackSlot`、hidden class、方法 inline cache 和运行时热度信息，对热点函数或热点循环做类型专门化和优化编译。

## 当前前置条件

已经具备：

- bytecode VM 和 `Chunk` 指令格式。
- 普通对象 hidden class：`ObjShape + slots`。
- 属性读写和方法调用的 polymorphic inline cache。
- `JitFeedback` 的调用计数和回边计数。
- `bytecode_decoder` 的指令解析和轻量 verifier。

仍需要 Baseline JIT 先补齐：

- baseline compiled code 表示。
- compiled code 执行入口。
- safepoint / fallback 协议。
- bytecode offset 到执行状态的映射。

Optimize JIT 不应该直接跳过 Baseline JIT。Baseline 是语义等价和执行入口的地基；Optimize JIT 是在这个地基上做猜测、守卫和回退。

## 代码位置

- 热度和 tiering 起点：[include/minijs/bytecode_function.h](../include/minijs/bytecode_function.h)、[include/minijs/vm.h](../include/minijs/vm.h)、[src/vm.cpp](../src/vm.cpp)
- bytecode 解码：[include/minijs/bytecode_decoder.h](../include/minijs/bytecode_decoder.h)、[src/bytecode_decoder.cpp](../src/bytecode_decoder.cpp)
- shape 和对象布局：[include/minijs/gc_object.h](../include/minijs/gc_object.h)、[docs/hidden-class.md](hidden-class.md)
- inline cache 反馈：[include/minijs/chunk.h](../include/minijs/chunk.h)、[docs/inline-cache.md](inline-cache.md)
- VM helper 和 GC 边界：[include/minijs/vm.h](../include/minijs/vm.h)、[src/vm.cpp](../src/vm.cpp)、[docs/gc.md](gc.md)
- Baseline JIT 前置设计：[docs/baseline-jit.md](baseline-jit.md)

建议新增：

- `include/minijs/optimizing_jit.h`
- `src/optimizing_jit.cpp`
- `include/minijs/optimizer_ir.h`
- `src/optimizer_ir.cpp`
- `include/minijs/deoptimizer.h`
- `src/deoptimizer.cpp`
- `include/minijs/jit_code.h`
- `src/jit_code.cpp`

## 目标

Optimize JIT 的目标是让热点代码走更少的动态检查：

- 用类型反馈把通用 `Value` 运算专门化为 number/string/object fast path。
- 用 shape feedback 把属性访问降为 guard + slot load/store。
- 用 class/method feedback 把方法调用降为 guard + direct call。
- 对热点循环做基本块级优化。
- 在猜测失效时 deopt 回 baseline 或 VM，并保持语义一致。

## 非目标

第一版 Optimize JIT 不做：

- 完整 ECMAScript 兼容优化。
- 全程序优化。
- 复杂对象逃逸分析。
- 高级内联策略。
- 跨函数全局类型推导。
- 复杂 GC 压缩或移动对象支持。

MiniJS 的 Optimize JIT 应该保持教学项目可解释性，宁可少优化，也要让 guard、deopt、feedback 的因果关系清楚。

## Tiering 模型

建议分三层：

```text
Interpreter / VM
  -> Baseline JIT
  -> Optimize JIT
```

当前 `JitState` 只有 `Cold`、`Scheduled`、`Compiled`、`Failed`。进入多 tier 后，建议拆成更明确的结构：

```cpp
enum class JitTier {
  None,
  Baseline,
  Optimized,
};

enum class JitState {
  Cold,
  BaselineScheduled,
  BaselineCompiled,
  OptimizeScheduled,
  OptimizedCompiled,
  Failed,
};
```

或者保留 `JitState`，但在 `JitFeedback` 里增加：

```cpp
BaselineCode* baseline = nullptr;
OptimizedCode* optimized = nullptr;
std::uint32_t optimizeCallCount = 0;
std::uint32_t optimizeBackedgeCount = 0;
std::uint32_t deoptCount = 0;
```

推荐第二种，改动较小，也能兼容当前状态机。

## 触发策略

Optimize JIT 不应该在函数第一次变热时触发。建议条件：

- 函数已经有 baseline code。
- baseline code 执行次数超过优化阈值。
- 函数没有频繁 deopt。
- bytecode verifier 和 stack verifier 都通过。
- feedback slot 状态有足够稳定性，例如 monomorphic 或 polymorphic，但不是 megamorphic。

触发来源：

- 函数入口调用计数。
- loop backedge 计数。
- baseline code 中的 OSR 计数。

第一版可以只做函数入口优化，不做 OSR。第二版再在 `Opcode::Loop` 对应位置支持 OSR entry。

## 反馈来源

Optimize JIT 的核心输入不是源码，而是 bytecode + runtime feedback：

- `FeedbackSlot::property`：普通对象 `ObjShape*` 到 slot 的映射。
- `FeedbackSlot::method`：`ObjClass*` 到 `ObjClosure*` 的映射。
- `FeedbackState`：判断访问点是否稳定。
- `JitFeedback`：函数级热度、回边热度、deopt 次数。
- 未来可加 `ValueType` profile：算术、比较、call 参数和返回值的常见类型。

当前已有 feedback 足够支持第一批优化：

- monomorphic shape 的 `obj.name`。
- polymorphic shape 数量小于等于 4 的 `obj.name`。
- monomorphic class 的 `obj.method()`。
- number-only 算术热点路径。

## IR 管线

建议 Optimize JIT 使用 SSA 风格 IR：

```text
Bytecode
  -> Decode
  -> CFG
  -> Stack-to-SSA
  -> Type feedback attach
  -> Guards
  -> Optimization passes
  -> Lowering
  -> Codegen
```

关键点：

- bytecode 是 stack machine，优化前要把 stack values 转成 SSA values。
- 每个 bytecode offset 需要记录 environment state，用于 deopt。
- `FeedbackSlot` 绑定到对应 IR 节点，例如 `LoadProperty`、`StoreProperty`、`CallMethod`。
- 复杂 opcode 可以先 lower 成 runtime call。

## 核心 IR 节点

第一版 IR 可以很小：

```text
Constant
LoadLocal
StoreLocal
LoadGlobal
StoreGlobal
AddNumber
SubNumber
MulNumber
DivNumber
CompareNumber
GuardType
GuardShape
GuardClass
LoadSlot
StoreSlot
Call
RuntimeCall
Branch
Jump
Return
Deopt
```

其中 `Guard*` 是 Optimize JIT 和 Baseline JIT 的核心区别。Baseline 保守执行；Optimize 先做猜测，猜错后 deopt。

## 属性访问优化

普通对象属性读取：

```javascript
obj.name
```

如果 feedback 显示访问点稳定为一个 shape：

```text
GuardShape(obj, cachedShape)
value = LoadSlot(obj, cachedSlot)
```

如果是小规模 polymorphic：

```text
if shape == shape0 -> LoadSlot(slot0)
if shape == shape1 -> LoadSlot(slot1)
...
Deopt
```

写入已有属性：

```text
GuardShape(obj, cachedShape)
StoreSlot(obj, cachedSlot, value)
```

写入新属性、dictionary mode 和 missing property 第一版都走 runtime call。

## 方法调用优化

实例方法调用：

```javascript
obj.get()
```

如果 method cache 稳定：

```text
GuardClass(obj, cachedClass)
CallDirect(cachedMethodClosure)
```

后续可以做小函数内联，但第一版只做 direct call，保留现有调用约定和 arity check。

静态方法同理，用 `isStatic=true` 的 method cache entry。

`super` 第一版不优化，继续 runtime call。因为 `super` 依赖当前 method 的 superclass 环境，deopt state 更复杂。

## 数值运算优化

如果 profile 显示某个 `Add/Sub/Mul/Div/Mod` 访问点长期只看到 number：

```text
GuardType(left, Number)
GuardType(right, Number)
result = AddNumber(left, right)
```

`+` 比其他算术更复杂，因为它还支持字符串拼接。第一版可以只优化 `Sub/Mul/Div/Mod` 和比较；`Add` 需要有明确 number-only 反馈后再优化。

## Deopt 设计

Optimize JIT 必须能从任意 guard 失败位置还原到 baseline/VM：

```text
DeoptState {
  bytecodeOffset
  stackValues
  localValues
  upvalueValues
  returnSlot
  slotStart
}
```

guard 失败时：

1. 根据 `DeoptState` 还原 VM stack 和 frame。
2. 增加 `deoptCount`。
3. 跳回 baseline code 或 VM 对应 bytecode offset。
4. 如果同一函数 deopt 过多，禁用 optimized code。

第一版可以选择更简单的函数级回退：guard 失败后丢弃 optimized code，重新从函数入口走 baseline/VM。这样实现简单，但不能保留副作用执行到一半的状态；因此只有在 guard 放在副作用之前时才安全。要优化带副作用的中间点，仍需要完整 deopt state。

## OSR

OSR 用于从正在运行的热点循环进入 optimized code。

第一版不建议做 OSR。原因：

- 当前回边只记录热度，VM 没有保存 loop header 的完整环境快照。
- OSR 需要把当前 operand stack、locals、upvalues 映射到 optimized frame。
- 需要 loop header 的 deopt/entry state。

建议路线：

1. 先支持函数入口 optimized code。
2. 在 CFG 构建时标记 loop header。
3. 为 loop header 生成 OSR entry descriptor。
4. 在 baseline 或 VM 的 loop backedge 检查 optimized OSR entry。

## GC 与 compiled code

Optimize JIT 可能让值进入寄存器或 native stack，因此必须有 safepoint 信息：

- 每个 runtime call 都是 safepoint。
- 每个可能分配对象的位置都是 safepoint。
- safepoint 记录 live `Value` 所在位置。
- compiled code 持有的 `ObjShape*`、`ObjClass*`、`ObjClosure*` 必须可标记或保证由其他 root 保活。

当前 feedback cache 已经由 `ObjFunction` 的 chunk 标记 shape/class/method closure。未来如果 optimized code 把这些指针复制到 code object 中，code object 也必须被 GC 标记扫描。

## 失效策略

MiniJS 当前对象模型让第一版 invalidation 可以很简单：

- 新增普通对象属性会产生新 shape，旧 `GuardShape` 自动失败。
- 删除属性会进入 dictionary mode，shape guard 或 fast path 失败。
- class method 表当前不是普通用户可变对象，method direct call 不需要复杂 invalidation。
- inline cache megamorphic 的访问点不生成 optimized fast path。

后续如果允许动态改 class method 或 prototype，就需要 code invalidation。

## 优化 pass 顺序

第一版 pass 可以保持克制：

1. Build CFG
2. Stack-to-SSA
3. Attach feedback
4. Insert guards
5. Constant folding
6. Dead code elimination
7. Branch simplification
8. Lower to baseline/native backend

等这些稳定后，再考虑：

- Common subexpression elimination
- Bounds check elimination
- Function inlining
- Escape analysis

## 测试计划

建议新增：

- optimized code 与 VM 结果一致。
- number-only 算术 guard 命中。
- number guard 失败后 deopt，结果仍正确。
- monomorphic property load 使用 shape + slot。
- shape guard 失败后 deopt。
- polymorphic property load 支持 2 到 4 个 shape。
- megamorphic feedback 不触发优化。
- method direct call 命中。
- method class guard 失败后 deopt。
- 手动 GC 后 optimized code 中的 cached shape/class/method 仍安全。
- deopt 次数过多后禁用 optimized code。

## 实现里程碑

1. 完成 Baseline JIT 和 compiled function dispatch。
2. 为 Baseline code 增加 bytecode offset 到 environment state 的映射。
3. 增加 `OptimizedCode` handle 和优化阈值。
4. 构建 CFG 和 stack-to-SSA IR。
5. 支持 number-only 算术优化和 guard/deopt。
6. 支持 monomorphic property load/store。
7. 支持 polymorphic property load。
8. 支持 monomorphic method direct call。
9. 增加 safepoint 和 code object GC 标记。
10. 再考虑 OSR 和函数内联。
