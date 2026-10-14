# FeedbackSlot 与 Inline Cache

本文档记录 MiniJS bytecode VM 当前 inline cache 的实现。当前缓存覆盖普通对象属性读取、普通对象属性写入、实例方法调用和静态方法调用；数组 `push`/`pop`、dictionary mode 对象和错误路径会绕过对应缓存。

## 设计目标

MiniJS 的 hidden class 让普通对象拥有稳定的 `ObjShape + slot` 布局。inline cache 进一步利用这一点：同一个字节码访问点再次遇到相同 shape 或 class 时，可以跳过名称查找，直接读取 slot 或调用已解析的方法。

当前实现不是早期设计里的单态 `Chunk` side table，而是统一的 `FeedbackSlot` 模型：

- 每个属性访问或方法调用指令带一个 feedback slot 操作数。
- slot 保存在 `Chunk` 中，生命周期跟字节码函数一致。
- 一个 slot 最多缓存 4 个条目，因此可以从 monomorphic 扩展到 polymorphic。
- 超过容量后进入 megamorphic 状态，后续走慢路径。

## 代码位置

- feedback slot 和 cache 数据结构：[include/minijs/chunk.h](../include/minijs/chunk.h) 中的 `FeedbackSlot`、`PropertyInlineCache`、`MethodInlineCache`
- cache 清理和 Chunk copy/move 语义：[src/chunk.cpp](../src/chunk.cpp)
- feedback slot 分配：[src/compiler.cpp](../src/compiler.cpp) 中的 `Compiler::emitExpression()`
- 属性读写 cache 查找和更新：[src/vm.cpp](../src/vm.cpp) 中的 `findPropertyInlineCacheEntry()`、`updatePropertyFeedback()`、`Opcode::GetProperty`、`Opcode::SetProperty`
- 方法调用 cache 查找和更新：[src/vm.cpp](../src/vm.cpp) 中的 `findMethodInlineCacheEntry()`、`updateMethodFeedback()`、`Opcode::MethodCall`
- cache 统计和调试入口：[include/minijs/vm.h](../include/minijs/vm.h)、[src/vm.cpp](../src/vm.cpp) 中的 `PropertyInlineCacheStats`、`debugPropertyInlineCacheStats()`、`setInlineCachesEnabled()`
- GC 标记 cache 引用：[src/vm.cpp](../src/vm.cpp) 中的 `markObjectChildren(ObjFunction*)`
- benchmark 示例：[examples/property_inline_cache.js](../examples/property_inline_cache.js)
- 测试覆盖：[tests/test_bytecode.cpp](../tests/test_bytecode.cpp) 中的 property/method inline cache 测试

## 数据结构

`include/minijs/chunk.h` 中的核心结构如下：

```cpp
struct PropertyInlineCacheEntry {
  ObjShape* shape = nullptr;
  std::size_t slot = 0;
};

struct PropertyInlineCache {
  static constexpr std::size_t MaxEntries = 4;
  std::array<PropertyInlineCacheEntry, MaxEntries> entries{};
  std::size_t size = 0;
};

struct MethodInlineCacheEntry {
  ObjClass* klass = nullptr;
  ObjClosure* method = nullptr;
  bool isStatic = false;
};

struct MethodInlineCache {
  static constexpr std::size_t MaxEntries = 4;
  std::array<MethodInlineCacheEntry, MaxEntries> entries{};
  std::size_t size = 0;
};

struct FeedbackSlot {
  FeedbackKind kind;
  FeedbackState state = FeedbackState::Uninitialized;
  PropertyInlineCache property;
  MethodInlineCache method;
};
```

`FeedbackKind` ： `GetProperty`、`SetProperty` 和 `MethodCall`

`FeedbackState` ： `Uninitialized`、`Monomorphic`、`Polymorphic` 和 `Megamorphic`

`FeedbackSlot`：**把“运行时收集到的优化信息”搬到函数自己的 bytecode 结构旁边**。把 IC 从“VM 里的临时缓存”升级成“函数 bytecode 的运行时画像”。记录这个 bytecode 在运行时见过什么对象形态、能不能走 inline cache，以及后续 JIT 能不能基于这些信息做优化。



## 字节码格式

编译器为这些指令分配 feedback slot：

```text
OP_GET_PROPERTY nameIndex feedbackSlot
OP_SET_PROPERTY nameIndex feedbackSlot
OP_METHOD_CALL  nameIndex argc feedbackSlot
```

`feedbackSlot` 是当前 `Chunk::feedbackSlots()` 的下标，不是栈槽，也不是常量池下标。同一个源码访问点重复执行时，会访问同一个 slot 并积累运行时反馈。

`OP_SUPER_CALL` 当前仍只编码方法名和参数数量，不使用 method inline cache。

## 属性读取缓存

`OP_GET_PROPERTY` 的普通对象路径大致如下：

1. dictionary mode 对象直接 bypass。
2. 如果 feedback slot 不是 megamorphic，按对象当前 shape 查找缓存项。
3. 命中时直接读取 `object.slots[slot]`。
4. 未命中时走 `objectGetProperty()` 慢路径。
5. 如果属性存在并且 slot 合法，把 `shape + slot` 追加到缓存。
6. 如果缓存已满，slot 状态变为 megamorphic，后续 bypass。

缺失属性不会写入缓存，因此不会做 negative cache。新增属性会转换 shape，旧 shape 的缓存自然 miss；重新赋值已有属性不改变 slot，命中后仍能读到最新值；删除属性会进入 dictionary mode，因此不会错误命中旧 shape。

## 属性写入缓存

`OP_SET_PROPERTY` 复用同一组 property cache entry：

- 命中已有 shape 时直接写入 `object.slots[slot]`。
- 未命中但属性已存在时，慢路径定位 slot 后更新缓存。
- 写入新属性时先让对象进行 shape transition，再缓存新 shape 下的 slot。
- dictionary mode 对象直接 bypass。

赋值表达式仍保持用户可见行为：写入完成后把被赋的值留在栈顶。

## 方法调用缓存

`OP_METHOD_CALL` 缓存实例方法和静态方法分派结果：

- 实例方法 entry 使用 `klass + method + isStatic=false`。
- 静态方法 entry 使用 `klass + method + isStatic=true`。
- 命中后直接调用缓存的 `ObjClosure`。
- 不同 class 可以在同一访问点共存，最多 4 项。
- 超过容量后进入 megamorphic 并回到慢路径。

数组 `push` 和 `pop` 是 VM 内建路径，不进入 method cache；`super` 调用也不会污染 method inline cache。

## Cache 生命周期与 GC

`Chunk` 的 copy/move 构造和赋值会复制字节码、常量池和 feedback slot 形状，但会清空运行期缓存内容。这样不会把某个 VM heap 中的 shape、class 或 closure 指针带到另一个 VM 执行环境。

GC 会标记函数 chunk 的 feedback slots：

- property cache 标记缓存的 `ObjShape*`。
- method cache 标记缓存的 `ObjClass*` 和 `ObjClosure*`。

因此缓存中的 shape、class 和 method closure 在函数可达时也保持可达。

## 统计与调试入口

`PropertyInlineCacheStats` 同时记录属性和方法缓存统计：

```cpp
std::size_t hits;
std::size_t misses;
std::size_t updates;
std::size_t bypasses;

std::size_t setHits;
std::size_t setMisses;
std::size_t setUpdates;
std::size_t setBypasses;

std::size_t methodCallHits;
std::size_t methodCallMisses;
std::size_t methodCallUpdates;
std::size_t methodCallBypasses;
```

`MINIJS_TESTING` 下提供：

```cpp
PropertyInlineCacheStats debugPropertyInlineCacheStats() const;
void debugResetPropertyInlineCacheStats();
```

CLI benchmark 路径可以关闭 inline cache，用于比较缓存启用和禁用时的执行结果。

## 测试覆盖

测试覆盖了：

- 同 shape 重复属性读取命中。
- 多个 shape 在同一访问点形成 polymorphic cache。
- 超过容量后进入 megamorphic。
- dictionary mode、missing property 和 GC 后缓存行为。
- set-property cache 的命中、更新、容量和 dictionary fallback。
- 实例方法、静态方法、polymorphic method call、megamorphic method call 和 `super` 隔离。

## 历史备注

本文件合并并替代了原 `docs/superpowers` 下的 property inline cache 设计文档。旧文档描述的是第一阶段单态读取缓存；当前实现已经扩展到 `FeedbackSlot`、属性读写缓存和方法调用缓存。
