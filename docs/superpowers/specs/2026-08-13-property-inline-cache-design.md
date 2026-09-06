# Property Inline Cache 设计

## 目标

为 MiniJS bytecode VM 的普通对象属性读取实现第一阶段 monomorphic inline cache。

当前普通对象已经使用 hidden class，也就是 `ObjShape + slots`。但是 `Opcode::GetProperty` 在访问普通对象属性时，仍然每次通过属性名在 `object.shape->slots` 里查找 slot。这个阶段要让同一个字节码访问点在重复遇到同一种对象 shape 时，可以直接用缓存的 slot 下标读取属性。

目标包括：

- 为 `Opcode::GetProperty` 的普通对象路径增加 monomorphic inline cache。
- cache hit 时通过 `object.slots[slot]` 直接读取属性。
- cache miss 时保持现有 `objectGetProperty` 慢路径语义，并在可缓存时更新 cache。
- dictionary mode、missing property、非普通对象路径保持正确语义。
- 增加 hit、miss、update、bypass 的测试观测能力。
- 保持现有语言行为、GC 行为和 CLI 行为不变。

## 非目标

本阶段不包含：

- `Opcode::SetProperty` inline cache。
- polymorphic inline cache。
- instance fields、method lookup、static method、`super` 优化。
- array 和 string 伪属性优化。
- negative cache，也就是缓存属性缺失结果。
- JIT 或 native code generation。
- 改变用户可见的对象属性语义。

## 背景

hidden class 阶段已经完成以下前置能力：

- `ObjObject` 保存 `ObjShape* shape` 和 `std::vector<Value> slots`。
- `ObjShape::slots` 保存属性名到 slot 下标的映射。
- 同样属性添加顺序的普通对象共享同一个最终 shape。
- 删除属性会让普通对象进入 `dictionaryMode`。
- 普通对象属性访问已经集中到 `VM::objectGetProperty` 等 helper。

这让 inline cache 可以只缓存两个信息：

```cpp
ObjShape* shape;
std::size_t slot;
```

当同一个 `OP_GET_PROPERTY` 再次访问拥有相同 `shape` 的普通对象时，就能绕过属性名查找。

## 方案选择

### 方案 A：`Chunk` side table，推荐

在 `Chunk` 内部维护一个按 opcode offset 索引的 property inline cache side table。

优点：

- cache 生命周期跟字节码访问点一致。
- key 是指令位置，语义清楚。
- VM 不需要维护跨 chunk 的外部 key。
- 后续扩展成 per-opcode metadata 更自然。

代价：

- `Chunk` 会持有 VM 运行期 `ObjShape*`，因此 copy 行为必须清空 cache。
- `Chunk` 的 cache accessor 需要是 `mutable`，因为 VM 当前以 `const Chunk&` 读取字节码。

### 方案 B：`VM` map keyed by `(Chunk*, opcodeOffset)`

优点是改动少，不需要改 `Chunk` 数据模型。

缺点是 key 依赖 `Chunk*` 地址，容易被 chunk 生命周期和地址复用影响。这个方案也让 cache 生命周期从字节码访问点转移到 VM，概念上更散。

### 方案 C：改字节码格式，给 `OP_GET_PROPERTY` 加 cache index operand

优点是更接近后续成熟 VM 的 metadata 设计。

缺点是会牵连 compiler、disassembler 和现有字节码格式。第一阶段只做 monomorphic get-property cache，暂时没有必要承担这个复杂度。

最终选择方案 A。

## 数据模型

在 `include/minijs/chunk.h` 中前置声明 `ObjShape`，并新增：

```cpp
struct PropertyInlineCache {
  bool initialized = false;
  ObjShape* shape = nullptr;
  std::size_t slot = 0;
};
```

在 `Chunk` 中新增：

```cpp
PropertyInlineCache& propertyInlineCache(std::size_t opcodeOffset) const;
void clearInlineCaches() const;

mutable std::unordered_map<std::size_t, PropertyInlineCache> propertyInlineCaches_;
```

`propertyInlineCaches_` 的 key 是 `OP_GET_PROPERTY` 的 opcode offset。也就是说：

```text
0008 OP_GET_GLOBAL 4 p
0010 OP_GET_PROPERTY 5 age
```

这里 cache key 是 `10`，不是 name constant index `5`。这样同一个源码访问点在循环中重复执行时，会持续使用同一个 cache entry。

## Chunk Copy/Move 语义

`PropertyInlineCache` 里保存 `ObjShape*`，而 shape 属于某个 VM 的 GC heap。为了避免把一个 VM 的 shape 指针复制到另一个 VM，`Chunk` 的复制操作必须不复制 inline cache。

新增显式 copy/move constructor 和 copy/move assignment：

```cpp
Chunk(const Chunk& other);
Chunk& operator=(const Chunk& other);
Chunk(Chunk&& other) noexcept;
Chunk& operator=(Chunk&& other) noexcept;
```

它们只复制或移动：

- `code_`
- `constants_`

不复制或移动：

- `propertyInlineCaches_`

这样无论 `Chunk` 是复制还是移动，目标对象都会从空 cache 开始。运行期 cache 只属于当前 `Chunk` 对象在当前 VM 中的执行历史，不作为可移植字节码数据的一部分。

## VM 执行流程

`Opcode::GetProperty` 当前先读取属性名常量，再根据 receiver 类型分流。

新流程保持现有分流顺序：

1. 记录 `opcodeOffset = frame.ip - 1`。
2. 读取 name constant。
3. pop receiver。
4. array `length` 路径保持不变。
5. string `length` 路径保持不变。
6. instance field 和 method lookup 路径保持不变。
7. class static method 路径保持不变。
8. 非普通对象继续抛出 `RuntimeError("value is not an object")`。
9. 普通对象进入 inline cache 路径。

普通对象路径：

```cpp
ObjObject* objectValue = object.asGcObject();

if (objectValue->dictionaryMode) {
  ++propertyInlineCacheStats_.bypasses;
  push(objectGetProperty(*objectValue, name));
  break;
}

PropertyInlineCache& cache = chunk.propertyInlineCache(opcodeOffset);
if (cache.initialized && cache.shape == objectValue->shape && cache.slot < objectValue->slots.size()) {
  ++propertyInlineCacheStats_.hits;
  push(objectValue->slots[cache.slot]);
  break;
}

++propertyInlineCacheStats_.misses;
Value result = objectGetProperty(*objectValue, name);

auto slot = objectValue->shape->slots.find(name);
if (slot != objectValue->shape->slots.end()) {
  cache.initialized = true;
  cache.shape = objectValue->shape;
  cache.slot = slot->second;
  ++propertyInlineCacheStats_.updates;
}

push(result);
```

这里不缓存 missing property。缺失属性继续走慢路径返回 `undefined`，并且不更新 cache。

## Cache 正确性

不需要主动 invalidation，原因是：

- 新增属性会让对象转换到新的 `shape`，旧 cache 的 shape 比较会 miss。
- 重新赋值已有属性不会改变 slot，cache hit 仍能读到最新值。
- 删除属性会让对象进入 `dictionaryMode`，普通对象 cache 路径会直接 bypass。
- 当前 root shape 持有 shape transition tree，live VM 中的 shape 不会被 GC 回收。
- `PropertyInlineCache` 不作为 GC root，因为它只缓存 shape 指针，不缓存 `Value` 或普通对象。shape 的可达性仍由 root shape transition tree 保证。

## Debug Hook

在 `MINIJS_TESTING` 下为 `VM` 增加：

```cpp
struct PropertyInlineCacheStats {
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t updates = 0;
  std::size_t bypasses = 0;
};

PropertyInlineCacheStats debugPropertyInlineCacheStats() const;
void debugResetPropertyInlineCacheStats();
```

统计含义：

- `hits`：普通对象 shape mode cache 命中。
- `misses`：普通对象 shape mode cache 未命中。
- `updates`：miss 后找到属性 slot，并写入 cache。
- `bypasses`：普通对象 dictionary mode 绕过 cache。

数组、字符串、instance、class 和错误路径不计入这些普通对象 IC 统计。

## 测试

### 命中测试

用循环保证重复执行同一个 `OP_GET_PROPERTY` 访问点：

```js
let p = { name: "Tom" };
let i = 0;
let value = "";
while (i < 3) {
  value = p.name;
  i = i + 1;
}
value;
```

期望：

- 结果是 `"Tom"`。
- `misses == 1`。
- `updates == 1`。
- `hits >= 2`。

### 相同 shape 测试

同一个访问点交替读取两个属性布局相同的对象：

```js
let a = { name: "Tom" };
let b = { name: "Ada" };
let i = 0;
let current = a;
let value = "";
while (i < 4) {
  if (i == 1) { current = b; }
  value = current.name;
  i = i + 1;
}
value;
```

期望第二个对象因为 shape 相同而命中 cache，用户可见结果仍正确。

### 不同 shape 测试

同一个访问点交替读取不同 shape 的对象：

```js
let a = { name: "Tom", age: 18 };
let b = { age: 20, name: "Ada" };
let i = 0;
let current = a;
let value = "";
while (i < 4) {
  if (i == 1) { current = b; }
  value = current.name;
  i = i + 1;
}
value;
```

期望出现 miss 和 cache update，结果仍是最后读取到的正确值。

### Dictionary Mode 测试

先让对象缓存命中，再删除属性触发 dictionary mode，然后继续读取：

```js
let p = { name: "Tom", age: 18 };
let i = 0;
let value = "";
while (i < 2) {
  value = p.name;
  i = i + 1;
}
del(p, "age");
p.name;
```

期望：

- 删除后读取仍返回 `"Tom"`。
- dictionary mode 读取增加 `bypasses`。
- 不错误命中删除前的 shape cache。

### Missing Property 测试

```js
let p = { name: "Tom" };
p.missing;
```

期望：

- 返回 `undefined`。
- 可以产生 miss。
- 不产生 update。

### 非普通对象路径测试

已有 array、string、instance、class 测试继续通过。新增断言可确认这些路径不改变普通对象 IC stats。

## 实现顺序

1. 在 `tests/test_bytecode.cpp` 增加同一访问点重复读取的失败测试。
2. 在 `include/minijs/chunk.h` 和 `src/chunk.cpp` 增加 `PropertyInlineCache`、side table accessor，以及不复制/移动 cache 的 `Chunk` copy/move 语义。
3. 在 `include/minijs/vm.h` 增加 `PropertyInlineCacheStats` 和测试 debug hook。
4. 在 `src/vm.cpp` 的 `Opcode::GetProperty` 普通对象路径加入 cache hit 和 miss/update 逻辑。
5. 增加相同 shape、不同 shape、dictionary mode、missing property 的回归测试。
6. 增加 `examples/property_inline_cache.js`，作为手动 benchmark 和演示脚本。
7. 运行 `cmake --build build`。
8. 运行 `ctest --test-dir build --output-on-failure`。

## 成功标准

- 所有现有测试继续通过。
- 同一个 `OP_GET_PROPERTY` 访问点重复读取同一 shape 对象时能观察到 cache hit。
- 不同 shape 访问同一属性时会 miss，并正确更新 cache。
- dictionary mode 对象不会错误命中旧 cache。
- missing property 仍返回 `undefined`。
- array、string、instance、class、method lookup、static method 和 `super` 语义不变。
- cache 不改变 GC object count 的用户可见结果。
- CLI 默认 VM 路径和 `--run` 行为不变。
