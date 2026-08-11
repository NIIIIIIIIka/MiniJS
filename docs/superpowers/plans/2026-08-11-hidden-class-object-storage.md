# Hidden Class Object Storage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 MiniJS VM 的普通对象从 `unordered_map<std::string, Value>` 存储升级为 hidden class/shape + slot 存储，并保持现有语言行为、GC 行为和 CLI 行为不变。

**Architecture:** 先引入普通对象属性辅助函数，把 VM 指令、内置函数、返回值转换和对象内部存储隔离开。随后新增 `ObjShape` 和 VM root shape，把普通对象改成 `shape + slots`，删除属性时退化到 dictionary mode。最后用测试专用 debug hook 验证 shape 共享、不同属性顺序产生不同 shape、删除后进入 dictionary mode，以及 GC 能正确标记 slot/dictionary 中的值。

**Tech Stack:** C++17、MiniJS bytecode VM、手写 mark-sweep GC、CMake/CTest、项目自带 `tests/test_framework.h`。

## Global Constraints

- 只改 VM 普通对象路径，不改 AST interpreter 的对象实现。
- 默认 CLI 和 `--run` 已经是 VM 路径；本计划不引入 CLI 行为变化。
- 第一阶段不实现 inline cache。
- 第一阶段不迁移 `ObjInstance::fields`。
- 数组、字符串、class、method、static method、继承和 `super` 路径保持现状。
- `del` 后普通对象永久进入 dictionary mode，不重新回到 shape mode。
- 所有属性值为 `Value`，shape 只保存属性名到 slot 下标的布局信息。
- shape 对象属于 VM 内部对象，不能改变已有 `objectCount()` 用户可见计数预期。
- 任何可能触发 GC 的对象属性写入，必须保证 receiver 和 value 在 GC roots 中。

---

## 执行前准备

当前工作区已有上一阶段 VM 主线改动和文档改动。开始 hidden class 代码实现前，先把当前状态整理成清晰提交。

- [ ] **Step 1: 查看工作区**

Run:

```bash
git status --short --branch
```

Expected: 能看到 `CMakeLists.txt`、`src/main.cpp`、`tests/fixtures/cli_vm_only.js`，以及 hidden class 文档相关改动。

- [ ] **Step 2: 验证当前基线测试**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 全部测试通过。

- [ ] **Step 3: 单独提交 VM 主线改动**

Run:

```bash
git add CMakeLists.txt src/main.cpp tests/fixtures/cli_vm_only.js
git commit -m "feat: make VM the default CLI path"
```

Expected: 新提交只包含 CLI/CTest/fixture 相关改动。

- [ ] **Step 4: 单独提交中文设计文档和本实施计划**

Run:

```bash
git add docs/superpowers/specs/2026-08-11-hidden-class-design.md docs/superpowers/plans/2026-08-11-hidden-class-object-storage.md
git commit -m "docs: add hidden class implementation plan"
```

Expected: 新提交只包含文档改动。

---

## 文件结构

- Modify: `include/minijs/object.h`
  - 增加 `ObjType::Shape`。

- Modify: `include/minijs/gc_object.h`
  - 增加 `ObjShape`。
  - 把 `ObjObject` 从 `properties` 改为 `shape + slots + dictionaryMode + dictionary`。

- Modify: `include/minijs/vm.h`
  - 增加普通对象属性辅助函数声明。
  - 增加 shape 转换辅助函数声明。
  - 增加 `rootObjectShape_`。
  - 把 `builtinObjectCount_` 调整为能覆盖 builtin 和 shape 的内部对象计数。
  - 增加测试专用 debug hook。

- Modify: `src/gc_object.cpp`
  - 增加 `ObjShape` 构造函数。
  - 更新 `ObjObject` 构造函数。

- Modify: `src/vm.cpp`
  - 更新 `len`、`has`、`del`、`keys`。
  - 更新 `Opcode::Object`、`Opcode::GetProperty`、`Opcode::SetProperty`。
  - 更新 `VM::copyOutValue`。
  - 更新 `markRoots` 和 `markObjectChildren`。
  - 实现 object helper、shape transition、dictionary fallback 和 debug hook。

- Modify: `tests/test_bytecode.cpp`
  - 增加行为保持测试。
  - 增加 shape 共享测试。
  - 增加 dictionary fallback 测试。
  - 增加 GC 标记测试。

---

### Task 1: 先封装普通对象属性辅助函数，不改变存储结构

**Files:**
- Modify: `include/minijs/vm.h`
- Modify: `src/vm.cpp`
- Modify: `tests/test_bytecode.cpp`

**Interfaces:**
- Consumes: 当前 `ObjObject::properties`。
- Produces:
  - `std::size_t VM::objectPropertyCount(const ObjObject& object) const`
  - `bool VM::objectHasProperty(const ObjObject& object, const std::string& name) const`
  - `Value VM::objectGetProperty(const ObjObject& object, const std::string& name) const`
  - `void VM::objectSetProperty(ObjObject& object, const std::string& name, Value value)`
  - `bool VM::objectDeleteProperty(ObjObject& object, const std::string& name)`
  - `std::vector<std::string> VM::objectKeys(const ObjObject& object) const`

- [ ] **Step 1: 写失败测试，覆盖现有普通对象行为**

在 `tests/test_bytecode.cpp` 的对象相关测试附近加入：

```cpp
void testBytecodeObjectPropertyHelpersPreserveBehavior() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "p.age = p.age + 1;"
                            "p.city = \"Shanghai\";"
                            "has(p, \"name\") && has(p, \"city\") && p.age == 19 && len(p) == 3;")
             .toString() == "true");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "p.age = 20;"
                            "has(p, \"age\") && p.age == 20 && len(p) == 2;")
             .toString() == "true");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" }; p.missing;").isUndefined());
}
```

在 `main()` 的对象测试调用区域加入：

```cpp
testBytecodeObjectPropertyHelpersPreserveBehavior();
```

- [ ] **Step 2: 运行测试，确认当前行为仍是绿的**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS。这个任务的测试是回归保护，不要求先红。

- [ ] **Step 3: 在 `include/minijs/vm.h` 声明 helper**

在 `copyOutValue` 附近的 private 区域加入：

```cpp
  std::size_t objectPropertyCount(const ObjObject& object) const;
  bool objectHasProperty(const ObjObject& object, const std::string& name) const;
  Value objectGetProperty(const ObjObject& object, const std::string& name) const;
  void objectSetProperty(ObjObject& object, const std::string& name, Value value);
  bool objectDeleteProperty(ObjObject& object, const std::string& name);
  std::vector<std::string> objectKeys(const ObjObject& object) const;
```

如果 `ObjObject` 尚未前置声明，在 `include/minijs/vm.h` 顶部已有对象前置声明区域补上：

```cpp
struct ObjObject;
```

- [ ] **Step 4: 在 `src/vm.cpp` 实现 helper，内部仍使用 unordered_map**

放在 `VM::copyOutValue` 前面：

```cpp
std::size_t VM::objectPropertyCount(const ObjObject& object) const {
  return object.properties.size();
}

bool VM::objectHasProperty(const ObjObject& object, const std::string& name) const {
  return object.properties.find(name) != object.properties.end();
}

Value VM::objectGetProperty(const ObjObject& object, const std::string& name) const {
  auto property = object.properties.find(name);
  if (property == object.properties.end()) {
    return Value::undefined();
  }
  return property->second;
}

void VM::objectSetProperty(ObjObject& object, const std::string& name, Value value) {
  object.properties[name] = std::move(value);
}

bool VM::objectDeleteProperty(ObjObject& object, const std::string& name) {
  return object.properties.erase(name) > 0;
}

std::vector<std::string> VM::objectKeys(const ObjObject& object) const {
  std::vector<std::string> keys;
  keys.reserve(object.properties.size());
  for (const auto& property : object.properties) {
    keys.push_back(property.first);
  }
  return keys;
}
```

- [ ] **Step 5: 更新 VM/builtin/copy-out 调用点**

把这些直接访问替换掉：

```cpp
value.asGcObject()->properties.size()
object.asGcObject()->properties
object.asGcObject()->properties.erase(name)
object.asGcObject()->properties[name] = value
value.asGcObject()->properties
```

替换目标：

```cpp
objectPropertyCount(*value.asGcObject())
objectHasProperty(*object.asGcObject(), name)
objectDeleteProperty(*object.asGcObject(), name)
objectSetProperty(*object.asGcObject(), name, value)
objectKeys(*object.asGcObject())
objectGetProperty(*object.asGcObject(), name)
```

`VM::copyOutValue` 的普通对象分支改成：

```cpp
if (value.isGcObject()) {
  std::unordered_map<std::string, Value> properties;
  for (const std::string& key : objectKeys(*value.asGcObject())) {
    properties[key] = copyOutValue(objectGetProperty(*value.asGcObject(), key));
  }
  return Value(std::move(properties));
}
```

- [ ] **Step 6: 验证并提交**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 全部测试通过。

Commit:

```bash
git add include/minijs/vm.h src/vm.cpp tests/test_bytecode.cpp
git commit -m "refactor: route VM object properties through helpers"
```

---

### Task 2: 引入 `ObjShape`、root shape 和内部对象计数

**Files:**
- Modify: `include/minijs/object.h`
- Modify: `include/minijs/gc_object.h`
- Modify: `include/minijs/vm.h`
- Modify: `src/gc_object.cpp`
- Modify: `src/vm.cpp`
- Modify: `tests/test_bytecode.cpp`

**Interfaces:**
- Consumes: Task 1 的 object helper。
- Produces:
  - `struct ObjShape final : public Obj`
  - `ObjShape* VM::rootObjectShape_`
  - `T* VM::allocateInternalObject(Args&&... args)`
  - `bool VM::debugHasRootObjectShape() const`，仅测试构建启用。

- [ ] **Step 1: 写失败测试，要求 VM 拥有 root shape 且不改变 objectCount**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeVmRootShapeIsInternalObject() {
  minijs::VM vm;

  EXPECT(vm.debugHasRootObjectShape());
  EXPECT(vm.objectCount() == 0);

  vm.collectGarbage();

  EXPECT(vm.debugHasRootObjectShape());
  EXPECT(vm.objectCount() == 0);
}
```

在 `main()` 的 GC 测试调用区域加入：

```cpp
testBytecodeVmRootShapeIsInternalObject();
```

- [ ] **Step 2: 运行测试，确认编译失败**

Run:

```bash
cmake --build build
```

Expected: FAIL，报错包含 `debugHasRootObjectShape` 未声明。

- [ ] **Step 3: 为测试构建启用 debug hook 宏**

在 `CMakeLists.txt` 的 `minijs_tests` target 创建后加入：

```cmake
target_compile_definitions(minijs_tests PRIVATE MINIJS_TESTING)
```

- [ ] **Step 4: 增加 `ObjType::Shape`**

在 `include/minijs/object.h` 的 `ObjType` 中加入：

```cpp
  Shape,
```

建议放在 `Object` 前面，使 shape/object 两类对象在枚举中相邻。

- [ ] **Step 5: 增加 `ObjShape` 数据结构**

在 `include/minijs/gc_object.h` 的 `ObjObject` 前面加入：

```cpp
struct ObjShape final : public Obj {
  ObjShape(ObjShape* parent, std::string addedProperty);

  ObjShape* parent = nullptr;
  std::string addedProperty;
  std::unordered_map<std::string, std::size_t> slots;
  std::unordered_map<std::string, ObjShape*> transitions;
};
```

- [ ] **Step 6: 实现 `ObjShape` 构造函数**

在 `src/gc_object.cpp` 中加入：

```cpp
ObjShape::ObjShape(ObjShape* parent, std::string addedProperty)
    : Obj(ObjType::Shape), parent(parent), addedProperty(std::move(addedProperty)) {}
```

- [ ] **Step 7: 调整 VM 内部对象计数**

在 `include/minijs/vm.h` 中把：

```cpp
std::size_t builtinObjectCount_ = 0;
```

改成：

```cpp
std::size_t internalObjectCount_ = 0;
```

并加入：

```cpp
ObjShape* rootObjectShape_ = nullptr;

template <typename T, typename... Args>
T* allocateInternalObject(Args&&... args);
```

在 `allocateObject` 模板后面加入：

```cpp
template <typename T, typename... Args>
T* VM::allocateInternalObject(Args&&... args) {
  T* object = allocateObject<T>(std::forward<Args>(args)...);
  ++internalObjectCount_;
  return object;
}
```

- [ ] **Step 8: 让 builtin 和 root shape 使用内部对象计数**

在 `src/vm.cpp` 中把 `defineBuiltin` 改成：

```cpp
void VM::defineBuiltin(std::string name, std::size_t arity, NativeFn function) {
  auto* native =
      allocateInternalObject<ObjNativeFunction>(std::move(name), arity, std::move(function));
  globals_[native->name] = Value(native);
}
```

把 `objectCount()` 改成：

```cpp
std::size_t VM::objectCount() const {
  return heapObjectCount_ - internalObjectCount_;
}
```

在 `VM::VM()` 的最前面创建 root shape：

```cpp
VM::VM() {
  rootObjectShape_ = allocateInternalObject<ObjShape>(nullptr, "");

  defineBuiltin("print", 1, [](const std::vector<Value>& arguments) -> Value {
```

- [ ] **Step 9: GC 标记 root shape 和 shape children**

在 `markRoots()` 里加入：

```cpp
markObject(rootObjectShape_);
```

在 `markObjectChildren()` 的 switch 中加入：

```cpp
case ObjType::Shape: {
  auto* shape = static_cast<ObjShape*>(object);
  markObject(shape->parent);
  for (const auto& transition : shape->transitions) {
    markObject(transition.second);
  }
  break;
}
```

- [ ] **Step 10: 增加测试专用 root shape debug hook**

在 `include/minijs/vm.h` 的 public 区域加入：

```cpp
#ifdef MINIJS_TESTING
  bool debugHasRootObjectShape() const;
#endif
```

在 `src/vm.cpp` 加入：

```cpp
#ifdef MINIJS_TESTING
bool VM::debugHasRootObjectShape() const {
  return rootObjectShape_ != nullptr && rootObjectShape_->type == ObjType::Shape;
}
#endif
```

- [ ] **Step 11: 验证并提交**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 全部测试通过，已有 `objectCount()` 断言不需要改。

Commit:

```bash
git add CMakeLists.txt include/minijs/object.h include/minijs/gc_object.h include/minijs/vm.h src/gc_object.cpp src/vm.cpp tests/test_bytecode.cpp
git commit -m "feat: add VM object shapes as internal GC objects"
```

---

### Task 3: 把普通对象改为 shape + slots 存储，并实现 dictionary fallback

**Files:**
- Modify: `include/minijs/gc_object.h`
- Modify: `src/gc_object.cpp`
- Modify: `include/minijs/vm.h`
- Modify: `src/vm.cpp`
- Modify: `tests/test_bytecode.cpp`

**Interfaces:**
- Consumes: Task 2 的 `ObjShape`、`rootObjectShape_` 和 Task 1 的 object helper。
- Produces:
  - `ObjObject::shape`
  - `ObjObject::slots`
  - `ObjObject::dictionaryMode`
  - `ObjObject::dictionary`
  - `ObjShape* VM::transitionObjectShape(ObjShape* shape, const std::string& name)`
  - `void VM::objectMaterializeDictionary(ObjObject& object) const`

- [ ] **Step 1: 写行为测试，证明 shape mode 保持普通对象语义**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeShapeModeObjectPropertiesPreserveSemantics() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "p.age = 19;"
                            "p.city = \"Shanghai\";"
                            "p.name == \"Tom\" && p.age == 19 && p.city == \"Shanghai\" && "
                            "has(p, \"name\") && has(p, \"city\") && len(p) == 3;")
             .toString() == "true");

  EXPECT(runBytecodeProgram("let p = { name: \"Tom\" };"
                            "p.name = \"Jerry\";"
                            "p.name == \"Jerry\" && len(p) == 1;")
             .toString() == "true");
}
```

在 `main()` 的对象测试调用区域加入：

```cpp
testBytecodeShapeModeObjectPropertiesPreserveSemantics();
```

- [ ] **Step 2: 写 GC 压力行为测试，防止属性写入时 value 被提前回收**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeShapeSetKeepsAssignedValuesDuringGcPressure() {
  EXPECT(runBytecodeProgram("let p = {};"
                            "p.name = \"Tom\";"
                            "p.a0 = \"a0\";"
                            "p.a1 = \"a1\";"
                            "p.a2 = \"a2\";"
                            "p.a3 = \"a3\";"
                            "p.a4 = \"a4\";"
                            "p.a5 = \"a5\";"
                            "p.a6 = \"a6\";"
                            "p.a7 = \"a7\";"
                            "p.name;")
             .toString() == "Tom");
}
```

在 `main()` 的 GC 或对象测试调用区域加入：

```cpp
testBytecodeShapeSetKeepsAssignedValuesDuringGcPressure();
```

- [ ] **Step 3: 写删除后仍可读写的行为测试**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeObjectDeleteFallsBackToDictionarySemantics() {
  EXPECT(runBytecodeProgram("let p = { name: \"Tom\", age: 18 };"
                            "del(p, \"age\");"
                            "p.city = \"Shanghai\";"
                            "p.name == \"Tom\" && p.city == \"Shanghai\" && "
                            "!has(p, \"age\") && len(p) == 2;")
             .toString() == "true");
}
```

在 `main()` 的对象测试调用区域加入：

```cpp
testBytecodeObjectDeleteFallsBackToDictionarySemantics();
```

- [ ] **Step 4: 运行测试，确认仍然通过**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS。此时仍是 map 存储，这些测试是切换存储结构前的保护网。

- [ ] **Step 5: 更新 `ObjObject` 字段**

在 `include/minijs/gc_object.h` 中把 `ObjObject` 改成：

```cpp
struct ObjObject final : public Obj {
  explicit ObjObject(ObjShape* rootShape);

  ObjShape* shape = nullptr;
  std::vector<Value> slots;
  bool dictionaryMode = false;
  std::unordered_map<std::string, Value> dictionary;
};
```

在 `src/gc_object.cpp` 中把构造函数改成：

```cpp
ObjObject::ObjObject(ObjShape* rootShape) : Obj(ObjType::Object), shape(rootShape) {}
```

- [ ] **Step 6: 在 VM 私有区声明 shape transition 和 dictionary materialization**

在 `include/minijs/vm.h` 的 object helper 附近加入：

```cpp
ObjShape* transitionObjectShape(ObjShape* shape, const std::string& name);
void objectMaterializeDictionary(ObjObject& object) const;
```

- [ ] **Step 7: 实现 shape transition**

在 `src/vm.cpp` 的 object helper 附近加入：

```cpp
ObjShape* VM::transitionObjectShape(ObjShape* shape, const std::string& name) {
  auto existing = shape->transitions.find(name);
  if (existing != shape->transitions.end()) {
    return existing->second;
  }

  auto* next = allocateInternalObject<ObjShape>(shape, name);
  next->slots = shape->slots;
  next->slots[name] = next->slots.size();
  shape->transitions[name] = next;
  return next;
}
```

- [ ] **Step 8: 改写 object helper，优先使用 shape + slots**

把 Task 1 的 helper 改成：

```cpp
std::size_t VM::objectPropertyCount(const ObjObject& object) const {
  if (object.dictionaryMode) {
    return object.dictionary.size();
  }
  return object.slots.size();
}

bool VM::objectHasProperty(const ObjObject& object, const std::string& name) const {
  if (object.dictionaryMode) {
    return object.dictionary.find(name) != object.dictionary.end();
  }
  return object.shape->slots.find(name) != object.shape->slots.end();
}

Value VM::objectGetProperty(const ObjObject& object, const std::string& name) const {
  if (object.dictionaryMode) {
    auto property = object.dictionary.find(name);
    if (property == object.dictionary.end()) {
      return Value::undefined();
    }
    return property->second;
  }

  auto slot = object.shape->slots.find(name);
  if (slot == object.shape->slots.end()) {
    return Value::undefined();
  }
  return object.slots[slot->second];
}

void VM::objectSetProperty(ObjObject& object, const std::string& name, Value value) {
  if (object.dictionaryMode) {
    object.dictionary[name] = std::move(value);
    return;
  }

  auto slot = object.shape->slots.find(name);
  if (slot != object.shape->slots.end()) {
    object.slots[slot->second] = std::move(value);
    return;
  }

  object.shape = transitionObjectShape(object.shape, name);
  object.slots.push_back(std::move(value));
}

std::vector<std::string> VM::objectKeys(const ObjObject& object) const {
  std::vector<std::string> keys;
  if (object.dictionaryMode) {
    keys.reserve(object.dictionary.size());
    for (const auto& property : object.dictionary) {
      keys.push_back(property.first);
    }
    return keys;
  }

  keys.reserve(object.shape->slots.size());
  for (const auto& slot : object.shape->slots) {
    keys.push_back(slot.first);
  }
  return keys;
}

void VM::objectMaterializeDictionary(ObjObject& object) const {
  if (object.dictionaryMode) {
    return;
  }

  object.dictionary.clear();
  for (const auto& slot : object.shape->slots) {
    object.dictionary[slot.first] = object.slots[slot.second];
  }
  object.dictionaryMode = true;
  object.shape = rootObjectShape_;
  object.slots.clear();
}

bool VM::objectDeleteProperty(ObjObject& object, const std::string& name) {
  objectMaterializeDictionary(object);
  return object.dictionary.erase(name) > 0;
}
```

- [ ] **Step 9: 更新 `Opcode::Object`，确保属性值在 shape 分配时仍被栈保护**

把 object literal 分支改成下面这种结构：

```cpp
case Opcode::Object: {
  const std::uint8_t namesIndex = chunk.readByte(frame.ip++);
  const std::vector<Value>& names = chunk.constant(namesIndex).asArray();

  auto* object = allocateObject<ObjObject>(rootObjectShape_);
  TemporaryRootScope roots(*this);
  roots.add(object);

  const std::size_t valueStart = stack_.size() - names.size();
  for (std::size_t i = 0; i < names.size(); ++i) {
    const std::string& name = names[i].asString();
    objectSetProperty(*object, name, stack_[valueStart + i]);
  }

  for (std::size_t i = 0; i < names.size(); ++i) {
    pop();
  }

  push(Value(object));
  break;
}
```

这里的关键点是：调用 `objectSetProperty` 时，属性值还留在 VM 栈上；如果 shape 分配触发 GC，value 不会被提前回收。

- [ ] **Step 10: 更新 `Opcode::SetProperty`，确保 receiver 和 value 在 shape 分配时仍被栈保护**

把普通对象赋值路径改成先读栈、不先 pop：

```cpp
case Opcode::SetProperty: {
  const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
  const std::string& name = chunk.constant(nameIndex).asString();

  Value value = peek();
  Value object = stack_[stack_.size() - 2];

  if (isGcInstanceValue(object)) {
    gcInstanceFields(object)[name] = value;
    pop();
    pop();
    push(value);
    break;
  }

  if (!object.isGcObject()) {
    throw RuntimeError("value is not an object");
  }

  objectSetProperty(*object.asGcObject(), name, value);
  pop();
  pop();
  push(value);
  break;
}
```

- [ ] **Step 11: 确认 builtin `del` 使用 helper**

普通对象分支应为：

```cpp
if (object.isGcObject()) {
  return Value(objectDeleteProperty(*object.asGcObject(), name));
}
```

- [ ] **Step 12: 更新 GC 标记普通对象的 shape、slots 和 dictionary**

`src/vm.cpp` 中 `ObjType::Object` 分支必须改成：

```cpp
case ObjType::Object: {
  auto* objectValue = static_cast<ObjObject*>(object);
  markObject(objectValue->shape);
  for (const Value& slot : objectValue->slots) {
    markValue(slot);
  }
  for (const auto& property : objectValue->dictionary) {
    markValue(property.second);
  }
  break;
}
```

- [ ] **Step 13: 验证并提交**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 全部测试通过。

Commit:

```bash
git add include/minijs/gc_object.h include/minijs/vm.h src/gc_object.cpp src/vm.cpp tests/test_bytecode.cpp
git commit -m "feat: store VM objects with shapes and slots"
```

---

### Task 4: 增加 shape 共享和 dictionary mode 的内部结构测试

**Files:**
- Modify: `include/minijs/vm.h`
- Modify: `src/vm.cpp`
- Modify: `tests/test_bytecode.cpp`

**Interfaces:**
- Consumes: Task 3 完成后的 shape/dictionary 存储。
- Produces:
  - `const ObjShape* VM::debugGlobalObjectShape(const std::string& name) const`
  - `bool VM::debugGlobalObjectUsesDictionary(const std::string& name) const`

- [ ] **Step 1: 增加可复用的 VM 运行测试 helper**

在 `tests/test_bytecode.cpp` 顶部 `runBytecodeProgram` 后加入：

```cpp
void runBytecodeProgramOnVm(minijs::VM& vm, std::string_view source) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  (void)vm.run(chunk);
}
```

- [ ] **Step 2: 写 shape 共享测试**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeObjectsWithSamePropertyOrderShareShape() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let a = {};"
                         "a.x = 1;"
                         "a.y = 2;"
                         "let b = {};"
                         "b.x = 3;"
                         "b.y = 4;"
                         "true;");

  EXPECT(vm.debugGlobalObjectShape("a") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("a") == vm.debugGlobalObjectShape("b"));
}

void testBytecodeObjectsWithDifferentPropertyOrderUseDifferentShapes() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let a = {};"
                         "a.x = 1;"
                         "a.y = 2;"
                         "let b = {};"
                         "b.y = 3;"
                         "b.x = 4;"
                         "true;");

  EXPECT(vm.debugGlobalObjectShape("a") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("b") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("a") != vm.debugGlobalObjectShape("b"));
}
```

在 `main()` 的对象测试调用区域加入：

```cpp
testBytecodeObjectsWithSamePropertyOrderShareShape();
testBytecodeObjectsWithDifferentPropertyOrderUseDifferentShapes();
```

- [ ] **Step 3: 写 reassign 不改变 shape 的测试**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeReassigningExistingPropertyKeepsShape() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let a = {};"
                         "a.x = 1;"
                         "let b = {};"
                         "b.x = 2;"
                         "a.x = 3;"
                         "true;");

  EXPECT(vm.debugGlobalObjectShape("a") != nullptr);
  EXPECT(vm.debugGlobalObjectShape("a") == vm.debugGlobalObjectShape("b"));
}
```

在 `main()` 的对象测试调用区域加入：

```cpp
testBytecodeReassigningExistingPropertyKeepsShape();
```

- [ ] **Step 4: 写 dictionary mode 测试**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeDeletingPropertySwitchesObjectToDictionaryMode() {
  minijs::VM vm;
  runBytecodeProgramOnVm(vm,
                         "let p = { name: \"Tom\", age: 18 };"
                         "del(p, \"age\");"
                         "true;");

  EXPECT(vm.debugGlobalObjectUsesDictionary("p"));
}
```

在 `main()` 的对象测试调用区域加入：

```cpp
testBytecodeDeletingPropertySwitchesObjectToDictionaryMode();
```

- [ ] **Step 5: 运行测试，确认编译失败**

Run:

```bash
cmake --build build
```

Expected: FAIL，报错包含 `debugGlobalObjectShape` 或 `debugGlobalObjectUsesDictionary` 未声明。

- [ ] **Step 6: 声明 debug hook**

在 `include/minijs/vm.h` 的 public 区域已有 `#ifdef MINIJS_TESTING` 块中加入：

```cpp
  const ObjShape* debugGlobalObjectShape(const std::string& name) const;
  bool debugGlobalObjectUsesDictionary(const std::string& name) const;
```

- [ ] **Step 7: 实现 debug hook**

在 `src/vm.cpp` 的 `#ifdef MINIJS_TESTING` 区域加入：

```cpp
const ObjShape* VM::debugGlobalObjectShape(const std::string& name) const {
  auto global = globals_.find(name);
  if (global == globals_.end() || !global->second.isGcObject()) {
    return nullptr;
  }
  return global->second.asGcObject()->shape;
}

bool VM::debugGlobalObjectUsesDictionary(const std::string& name) const {
  auto global = globals_.find(name);
  if (global == globals_.end() || !global->second.isGcObject()) {
    return false;
  }
  return global->second.asGcObject()->dictionaryMode;
}
```

- [ ] **Step 8: 验证并提交**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 全部测试通过。

Commit:

```bash
git add include/minijs/vm.h src/vm.cpp tests/test_bytecode.cpp
git commit -m "test: cover hidden class shape sharing"
```

---

### Task 5: 补齐 shape/dictionary 的 GC 标记测试

**Files:**
- Modify: `tests/test_bytecode.cpp`
- Modify: `src/vm.cpp`

**Interfaces:**
- Consumes: Task 4 的内部结构测试和 Task 3 的 shape slot/dictionary marking。
- Produces: 明确覆盖 slot value 和 dictionary value 的 GC 回归测试。

- [ ] **Step 1: 写 shape slot 值标记测试**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeGcKeepsDynamicShapeSlotValue() {
  minijs::Parser parser("let p = {};"
                        "p.name = \"Tom\";"
                        "p;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isObject());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}
```

在 `main()` 的 GC 测试调用区域加入：

```cpp
testBytecodeGcKeepsDynamicShapeSlotValue();
```

- [ ] **Step 2: 写 dictionary mode 值标记测试**

在 `tests/test_bytecode.cpp` 加入：

```cpp
void testBytecodeGcKeepsDictionaryModeObjectValue() {
  minijs::Parser parser("let p = { old: 1 };"
                        "del(p, \"old\");"
                        "p.name = \"Tom\";"
                        "p;");
  minijs::Program program = parser.parseProgram();

  EXPECT(parser.diagnostics().empty());

  minijs::Compiler compiler;
  minijs::Chunk chunk = compiler.compileProgram(program);

  minijs::VM vm;
  EXPECT(vm.run(chunk).isObject());
  EXPECT(vm.objectCount() == 2);

  vm.collectGarbage();
  EXPECT(vm.objectCount() == 2);
}
```

在 `main()` 的 GC 测试调用区域加入：

```cpp
testBytecodeGcKeepsDictionaryModeObjectValue();
```

- [ ] **Step 3: 运行测试，确认如果 GC 标记未覆盖会失败**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: PASS。如果失败，失败通常说明 `ObjType::Object` 分支没有标记 `slots` 或 `dictionary`。

- [ ] **Step 4: 核对 `markObjectChildren` 的 object 分支**

`src/vm.cpp` 中 `ObjType::Object` 分支必须是：

```cpp
case ObjType::Object: {
  auto* objectValue = static_cast<ObjObject*>(object);
  markObject(objectValue->shape);
  for (const Value& slot : objectValue->slots) {
    markValue(slot);
  }
  for (const auto& property : objectValue->dictionary) {
    markValue(property.second);
  }
  break;
}
```

- [ ] **Step 5: 验证并提交**

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: 全部测试通过。

Commit:

```bash
git add src/vm.cpp tests/test_bytecode.cpp
git commit -m "test: cover GC marking for shaped objects"
```

---

## 最终验收

- [ ] **Step 1: 全量构建**

Run:

```bash
cmake --build build
```

Expected: build 成功。

- [ ] **Step 2: 全量测试**

Run:

```bash
ctest --test-dir build --output-on-failure
```

Expected: 全部测试通过。

- [ ] **Step 3: 搜索旧普通对象直接访问**

Run:

```bash
rg -n "asGcObject\\(\\)->properties|ObjObject::ObjObject\\(std::unordered_map|\\.properties" include src tests
```

Expected: 不再出现普通 `ObjObject::properties` 的访问。`ObjInstance::fields` 相关访问可以保留。

- [ ] **Step 4: 确认工作区只剩预期改动**

Run:

```bash
git status --short --branch
```

Expected: 工作区干净，或只剩明确不属于 hidden class 的用户改动。

---

## 面试表达重点

- 这不是简单把 `unordered_map` 换成数组，而是把对象 layout 和对象 value storage 拆开。
- hidden class/shape 负责记录属性名到 slot 的映射；对象自身只保存 `slots`。
- 相同属性添加顺序的对象共享 shape，后续 inline cache 可以缓存 `shape + slot index`。
- 删除属性会破坏稳定 layout，所以退化到 dictionary mode，保证语义简单正确。
- GC 不只标记对象本体，还要标记 shape graph、slot values 和 dictionary fallback values。
- 这一步完成后，inline cache 的实现会很自然：`GetProperty` 指令缓存上一次看到的 shape 指针和 slot 下标。
