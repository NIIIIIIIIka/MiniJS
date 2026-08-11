# Hidden Class 对象存储设计

## 目标

为 MiniJS VM 的普通对象引入第一阶段 hidden class 对象表示。

这一阶段会把普通 `ObjObject` 的属性存储从直接使用 `unordered_map<string, Value>` 查找，改成基于 shape 的 slot 存储。同时保持当前用户可见语义不变，包括对象字面量、属性 get/set、`len`、`has`、`keys`、`del`、GC 标记，以及 VM 返回值兼容性。

这一阶段不实现 inline cache，不迁移 instance field，不优化数组/字符串属性，不改 class method lookup，也不改 `super`。这些都放到普通对象表示稳定之后再做。

## 动机

当前 VM 的普通对象属性存在哈希表里。每次 `obj.name` 查找都要付出字符串 hash 和 map lookup 的成本。hidden class，也可以叫 shape，可以让属性添加顺序相同的对象共享同一份属性布局：

```text
empty shape
  -- name --> shape(name: slot 0)
  -- age  --> shape(name: slot 0, age: slot 1)

object.shape = shape(name, age)
object.slots = ["Tom", 18]
```

这会让 MiniJS 的运行时架构更接近真实 JavaScript 引擎，也为后续 monomorphic inline cache 打下清晰基础。

## 范围

本阶段包含：

- 普通对象字面量和普通对象属性赋值。
- 普通对象属性读取。
- 普通对象上的 `len`、`has`、`keys`、`del`。
- 普通对象的 VM GC 标记和 copy-out 兼容。
- 证明现有对象语义不变的测试。
- 证明属性添加顺序相同的对象会共享 shape 的测试。

本阶段不包含：

- inline cache 实现。
- `ObjInstance::fields` 迁移。
- class method lookup、static method、继承方法和 `super`。
- 数组和字符串的伪属性。
- dictionary fallback 之后重新回到 shape mode。
- 用 shape 转换优化属性删除。

## 数据模型

新增一个 GC 对象类型：

```cpp
struct ObjShape final : Obj {
  ObjShape* parent = nullptr;
  std::string addedProperty;
  std::unordered_map<std::string, std::size_t> slots;
  std::unordered_map<std::string, ObjShape*> transitions;
};
```

`slots` 记录当前 shape 中每个属性名对应的 slot 下标。`transitions` 记录“在当前 shape 上新增某个属性后会转移到哪个 shape”。

普通对象改成：

```cpp
struct ObjObject final : Obj {
  explicit ObjObject(ObjShape* rootShape);

  ObjShape* shape = nullptr;
  std::vector<Value> slots;

  bool dictionaryMode = false;
  std::unordered_map<std::string, Value> dictionary;
};
```

VM 持有一个 root empty shape：

```cpp
ObjShape* rootObjectShape_ = nullptr;
```

root shape 在 VM 构造期间通过 GC heap 分配，并且通过 VM roots 保持可达。

## 对象属性辅助函数

所有普通对象属性操作都应该走辅助函数，而不是在 VM 指令、内置函数或 GC 逻辑里直接访问字段：

```cpp
std::size_t objectPropertyCount(const ObjObject& object);
bool objectHasProperty(const ObjObject& object, const std::string& name);
Value objectGetProperty(const ObjObject& object, const std::string& name);
void objectSetProperty(VM& vm, ObjObject& object, const std::string& name, Value value);
bool objectDeleteProperty(ObjObject& object, const std::string& name);
std::vector<std::string> objectKeys(const ObjObject& object);
void objectMaterializeDictionary(ObjObject& object);
```

这些辅助函数把存储细节从 `Opcode::GetProperty`、`Opcode::SetProperty`、内置函数、GC 兼容代码，以及后续 inline cache 工作中隔离出来。

## Shape 转换

当对 shape mode 对象设置一个新属性时：

1. 检查当前 shape 是否已经包含这个属性。
2. 如果包含，直接更新已有 slot。
3. 如果不包含，检查 `shape->transitions[name]`。
4. 如果转换已存在，复用对应 shape。
5. 如果转换不存在，分配新的 `ObjShape`，复制父 shape 的 slots，追加 `name -> newSlot`，并记录转换。
6. 更新 `object.shape`，并把新值追加到 `object.slots`。

属性添加顺序相同的对象最终会收敛到同一个 shape。

## 对象字面量

`Opcode::Object` 目前会根据常量属性名和栈上的属性值构造一个 unordered map。改造后应该变成：

1. 用 root shape 分配一个 `ObjObject`。
2. 按源码顺序取出属性值。
3. 对每个属性名和值调用 `objectSetProperty`。
4. 把新对象值压回栈。

这里必须保留源码顺序，因为 hidden class 依赖属性添加顺序。现有 compiler 已经按顺序保存对象字面量属性名，所以 VM 在填充 slots 时也应该保持这个顺序。

## 属性读取

普通对象的 `Opcode::GetProperty` 应该调用 `objectGetProperty`。

shape mode 读取逻辑：

```cpp
if (!object.dictionaryMode) {
  auto slot = object.shape->slots.find(name);
  if (slot != object.shape->slots.end()) {
    return object.slots[slot->second];
  }
  return Value::undefined();
}
```

dictionary mode 读取使用 fallback dictionary，属性不存在时返回 `undefined`。

数组、字符串、class、instance、method、static method 和 `super` 路径在这一阶段保持不变。

## 属性写入

普通对象的 `Opcode::SetProperty` 应该调用 `objectSetProperty`。

shape mode 写入会更新已有 slot，或者转换到新 shape。dictionary mode 写入则直接更新 dictionary。

这个操作必须保留当前表达式结果行为：属性赋值后要把被赋的值重新压回 VM 栈。

## 删除与 Dictionary Fallback

`del(obj, key)` 不应该尝试从 shape 中移除属性。改用 fallback 策略：

1. 如果对象处于 shape mode，根据 `shape->slots` 和 `slots` 物化出 `dictionary`。
2. 设置 `dictionaryMode = true`。
3. 清空 `shape/slots`，或者保留但让所有属性辅助函数不再使用它们。
4. 从 `dictionary` 中删除目标 key。

进入 dictionary fallback 后，对象永久停留在 dictionary mode。这能保持删除语义简单，也符合“高度动态对象不走优化路径”的设计思路。

## 内置函数

更新内置函数里的普通对象处理：

- `len(object)` 使用 `objectPropertyCount`。
- `has(object, key)` 使用 `objectHasProperty`。
- `keys(object)` 使用 `objectKeys`。
- `del(object, key)` 使用 `objectDeleteProperty`。

instance 处理在这一阶段继续保持 map-based。

## GC

新增 `ObjType::Shape`，并在 GC 中标记 shape 对象。

普通对象标记：

```cpp
markObject(object->shape);
for (const Value& slot : object->slots) {
  markValue(slot);
}
for (const auto& entry : object->dictionary) {
  markValue(entry.second);
}
```

shape 标记：

```cpp
markObject(shape->parent);
for (const auto& transition : shape->transitions) {
  markObject(transition.second);
}
```

shape 的属性名是 C++ string，不需要 GC 标记。shape 转换必须被标记，这样一个 live shape 才能保持它的转换树可达。VM root shape 也必须加入 root set。

## 返回值转换兼容

`VM::copyOutValue` 当前会把 GC 普通对象转换回旧的 `Value` object map。改造后它应该通过 `objectKeys` 和 `objectGetProperty` 构造 map，而不是直接读取内部存储。

这样可以在改变 VM 内部表示的同时，保留现有测试和外部行为。

## 测试

行为测试：

- 对象字面量属性读取仍然正常。
- 动态属性赋值仍然正常。
- 重新赋值已有属性会更新旧值。
- 普通对象缺失属性仍然返回 `undefined`。
- 两个拥有相同属性的对象仍然保持值独立。
- `len`、`has`、`keys`、`del` 保持现有行为。
- `del` 后 get/set/has/keys/len 仍然能通过 dictionary mode 正常工作。

shape 测试：

- 两个都按 `x` 再 `y` 顺序添加属性的对象，会共享同一个最终 shape。
- `x` 再 `y` 和 `y` 再 `x` 两种添加顺序不会共享同一个最终 shape。
- 重新赋值已有属性不会改变 shape。
- 删除属性会让该对象切换到 dictionary mode。

GC 测试：

- shape mode 对象 slots 中的嵌套 string、array、object、class、closure 会保持可达。
- 不可达的 shape mode 对象会被回收。
- root object shape 在手动 GC 后仍保持存活。
- dictionary fallback 后，dictionary mode 对象里的值仍会被标记。

## 实现顺序

1. 先在现有 `unordered_map` 实现外面包一层辅助函数，并更新 VM/内置函数/copy-out 调用点，让它们都走辅助函数。
2. 新增 `ObjShape`、`ObjType::Shape`、VM root shape 分配和 GC 标记逻辑。
3. 把 `ObjObject` 改成 shape-mode 字段加 dictionary fallback。
4. 更新对象字面量创建，以及普通对象 get/set/delete 辅助函数。
5. 增加行为测试和 shape-sharing 测试。
6. 增加 slot marking 和 dictionary-mode marking 的 GC 测试。

inline cache 应该等这一阶段通过所有现有测试和新增 shape 测试后再开始。

## 成功标准

- 所有现有测试继续通过。
- 新增对象行为测试通过。
- 新增 shape-sharing 测试通过。
- 新增 GC 测试通过。
- 不引入 CLI 行为变化。
- 未删除属性的普通对象内部使用 shape/slots。
- `del` 能稳定地把普通对象切换到 dictionary mode。
