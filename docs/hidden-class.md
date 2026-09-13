# Hidden Class 与对象存储

本文档记录 MiniJS bytecode VM 当前普通对象的 hidden class 设计。这里的普通对象指对象字面量和动态属性访问产生的 `ObjObject`，不包含 `ObjInstance::fields`、数组、字符串、类方法表或 `super` 路径。

## 设计目标

早期普通对象直接使用 `unordered_map<string, Value>` 保存属性。这个结构简单，但每次 `obj.name` 都需要按字符串做哈希和查表，也没有一个稳定的对象布局可供 inline cache 使用。

当前实现把普通对象拆成两部分：

- `ObjShape` 保存属性名到 slot 下标的布局信息。
- `ObjObject` 保存指向 shape 的指针和实际属性值数组。

属性添加顺序相同的对象会共享同一个最终 shape。这样 VM 后续可以用 `shape + slot` 作为缓存键，避免重复查找属性名。

**Architecture:**

先引入普通对象属性辅助函数，把 VM 指令、内置函数、返回值转换和对象内部存储隔离开。随后新增 `ObjShape` 和 VM root shape，把普通对象改成 `shape + slots`，删除属性时退化到 dictionary mode。最后用测试专用 debug hook 验证 shape 共享、不同属性顺序产生不同 shape、删除后进入 dictionary mode，以及 GC 能正确标记 slot/dictionary 中的值。

- 数组、字符串、class、method、static method、继承和 `super` 路径保持现状。

- `del` 后普通对象永久进入 dictionary mode，不重新回到 shape mode。

- 所有属性值为 `Value`，shape 只保存属性名到 slot 下标的布局信息。

- shape 对象属于 VM 内部对象，不能改变已有 `objectCount()` 用户可见计数预期。

- 任何可能触发 GC 的对象属性写入，必须保证 receiver 和 value 在 GC roots 中。

## 代码位置

- shape 和普通对象结构：[include/minijs/gc_object.h](../include/minijs/gc_object.h) 中的 `ObjShape`、`ObjObject`
- shape 和对象构造函数：[src/gc_object.cpp](../src/gc_object.cpp)
- VM helper 声明和 root shape 字段：[include/minijs/vm.h](../include/minijs/vm.h) 中的 `objectGetProperty()`、`objectSetProperty()`、`objectDeleteProperty()`、`transitionObjectShape()`、`objectMaterializeDictionary()`、`rootObjectShape_`
- 对象字面量、属性读写和内置函数路径：[src/vm.cpp](../src/vm.cpp) 中的 `Opcode::Object`、`Opcode::GetProperty`、`Opcode::SetProperty`、`len`、`has`、`keys`、`del`
- shape 转换、dictionary fallback 和 copy-out：[src/vm.cpp](../src/vm.cpp) 中的 `transitionObjectShape()`、`objectMaterializeDictionary()`、`copyOutValue()`
- GC 标记路径：[src/vm.cpp](../src/vm.cpp) 中的 `markRoots()`、`markObjectChildren()`
- 测试覆盖：[tests/test_bytecode.cpp](../tests/test_bytecode.cpp) 中的 shape sharing、dictionary mode 和 GC 标记测试

## 数据结构

`include/minijs/gc_object.h` 中的核心结构如下：

```cpp
struct ObjShape final : public Obj {
  ObjShape* parent = nullptr;
  std::string addedProperty;
  // <property_name,offset>
  std::unordered_map<std::string, std::size_t> slots;
  // <new_property_name,new_shape>
  std::unordered_map<std::string, ObjShape*> transitions;
};

struct ObjObject final : public Obj {
  ObjShape* shape = nullptr;
  std::vector<Value> slots;
  bool dictionaryMode = false;
  std::unordered_map<std::string, Value> dictionary;
};
```

`VM` 持有一个内部 root shape：

```cpp
ObjShape* rootObjectShape_ = nullptr;
```

root shape 在 VM 构造时分配，并作为内部对象计数的一部分，不暴露到 `objectCount()` 的用户可见统计中。

## Shape 转换

shape mode 对象写入新属性时会沿转换树前进：

1. 如果当前 shape 已经有该属性，直接更新对应 slot。
2. 如果没有该属性，检查 `shape->transitions[name]`。
3. 已存在转换时复用目标 shape。
4. 不存在转换时创建新的 `ObjShape`，复制父 shape 的 `slots`，追加新属性到下一个 slot。
5. 对象更新到新 shape，并把属性值追加到 `slots`。

示意：

```text
root
  -- name --> shape(name: slot 0)
      -- age --> shape(name: slot 0, age: slot 1)
```

两个对象都按 `name` 再 `age` 的顺序写入属性时，会共享最后一个 shape；如果顺序是 `age` 再 `name`，则会走到另一条转换路径。

## 属性访问边界

普通对象属性操作集中在 `VM` 的 helper 中：

```cpp
std::size_t objectPropertyCount(const ObjObject& object) const;
bool objectHasProperty(const ObjObject& object, const std::string& name) const;
Value objectGetProperty(const ObjObject& object, const std::string& name) const;
void objectSetProperty(ObjObject& object, const std::string& name, Value value);
bool objectDeleteProperty(ObjObject& object, const std::string& name);
std::vector<std::string> objectKeys(const ObjObject& object) const;
ObjShape* transitionObjectShape(ObjShape* shape, const std::string& name);
void objectMaterializeDictionary(ObjObject& object) const;
```

这些 helper 隔离了内部存储细节。`Opcode::Object`、`Opcode::GetProperty`、`Opcode::SetProperty`、`len`、`has`、`keys`、`del` 和 `copyOutValue()` 都通过它们访问普通对象。

## Dictionary Fallback

`del(obj, key)` 不尝试从 shape 中移除属性。删除会先把对象物化为 dictionary mode：

1. 根据当前 shape 的 `slots` 把所有属性复制到 `dictionary`。
2. 设置 `dictionaryMode = true`。
3. 把 `shape` 重置到 root shape，并清空 `slots`。
4. 从 `dictionary` 删除目标属性。

进入 dictionary mode 后，对象不会再回到 shape mode。这让删除语义保持简单，也让高度动态的对象退出优化路径。

## GC 标记

普通对象和 shape 都由 VM 的 mark-sweep GC 管理。

普通对象标记：

```text
shape
slots 中的每个 Value
dictionary 中的每个 Value
```

shape 标记：

```text
parent
transitions 中的每个目标 shape
```

此外，`rootObjectShape_` 是 VM root。这样 live VM 中的 shape 转换树不会被提前清扫，inline cache 中缓存的 shape 指针也能保持有效。

## 测试与调试入口

测试侧主要覆盖：

- 对象字面量、动态属性写入、重新赋值和缺失属性行为保持不变。
- 相同属性添加顺序共享 shape，不同顺序不共享。
- 删除属性后进入 dictionary mode。
- shape mode 的 slot 值和 dictionary fallback 值都能被 GC 正确标记。

`MINIJS_TESTING` 下提供：

```cpp
const ObjShape* debugGlobalObjectShape(const std::string& name) const;
bool debugGlobalObjectUsesDictionary(const std::string& name) const;
```
