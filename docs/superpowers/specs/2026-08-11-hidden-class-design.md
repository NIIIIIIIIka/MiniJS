# Hidden Class Object Storage Design

## Goal

Introduce a first-stage hidden class object representation for MiniJS VM ordinary objects.

This phase changes ordinary `ObjObject` property storage from direct `unordered_map<string, Value>` lookup to shape-based slot storage while preserving the current user-visible semantics of object literals, property get/set, `len`, `has`, `keys`, `del`, GC marking, and VM return compatibility.

This phase does not implement inline caches, instance field shapes, array/string property optimization, class method lookup changes, or `super` changes. Those become follow-up phases after the ordinary object representation is stable.

## Motivation

The current VM object representation stores ordinary object properties in a hash table. Every `obj.name` lookup pays string hash and map lookup cost. A hidden class, also called a shape, lets objects with the same property addition order share a property layout:

```text
empty shape
  -- name --> shape(name: slot 0)
  -- age  --> shape(name: slot 0, age: slot 1)

object.shape = shape(name, age)
object.slots = ["Tom", 18]
```

This gives MiniJS a runtime architecture closer to real JavaScript engines and creates a clean foundation for later monomorphic inline caches.

## Scope

In scope:

- Ordinary object literals and ordinary object property assignment.
- Ordinary object property reads.
- `len`, `has`, `keys`, and `del` for ordinary objects.
- VM GC marking and copy-out compatibility for ordinary objects.
- Tests that prove current object semantics are preserved.
- Tests that prove objects with the same property addition order share a shape.

Out of scope:

- Inline cache implementation.
- `ObjInstance::fields` migration.
- Class method lookup, static methods, inherited methods, and `super`.
- Array and string pseudo-properties.
- Re-entering shape mode after dictionary fallback.
- Optimizing property deletion with shape transitions.

## Data Model

Add a new GC object type:

```cpp
struct ObjShape final : Obj {
  ObjShape* parent = nullptr;
  std::string addedProperty;
  std::unordered_map<std::string, std::size_t> slots;
  std::unordered_map<std::string, ObjShape*> transitions;
};
```

`slots` maps each property name to its slot index for this shape. `transitions` maps a property name to the next shape reached by adding that property.

Update ordinary objects to:

```cpp
struct ObjObject final : Obj {
  explicit ObjObject(ObjShape* rootShape);

  ObjShape* shape = nullptr;
  std::vector<Value> slots;

  bool dictionaryMode = false;
  std::unordered_map<std::string, Value> dictionary;
};
```

The VM owns one root empty shape:

```cpp
ObjShape* rootObjectShape_ = nullptr;
```

The root shape is allocated through the VM GC heap during VM construction and remains reachable through VM roots.

## Object Property Helpers

All ordinary object property operations should go through helper functions instead of touching fields directly:

```cpp
std::size_t objectPropertyCount(const ObjObject& object);
bool objectHasProperty(const ObjObject& object, const std::string& name);
Value objectGetProperty(const ObjObject& object, const std::string& name);
void objectSetProperty(VM& vm, ObjObject& object, const std::string& name, Value value);
bool objectDeleteProperty(ObjObject& object, const std::string& name);
std::vector<std::string> objectKeys(const ObjObject& object);
void objectMaterializeDictionary(ObjObject& object);
```

These helpers isolate storage details from `Opcode::GetProperty`, `Opcode::SetProperty`, builtins, GC compatibility code, and future inline cache work.

## Shape Transitions

When setting a new property on a shape-mode object:

1. Check whether the current shape already contains the property.
2. If yes, update the existing slot.
3. If no, check `shape->transitions[name]`.
4. If a transition exists, reuse that shape.
5. If no transition exists, allocate a new `ObjShape`, copy the parent slots, append `name -> newSlot`, and record the transition.
6. Update `object.shape` and append the new value to `object.slots`.

Objects with the same property addition order will converge to the same final shape.

## Object Literals

`Opcode::Object` currently builds an unordered map from constant property names and stack values. It should instead:

1. Allocate an `ObjObject` with the root shape.
2. Pop property values in source order.
3. Call `objectSetProperty` for each property name and value.
4. Push the new object value.

The source-order insertion requirement matters because hidden classes depend on property addition order. The existing compiler stores object literal property names in order, so the VM should preserve that order when populating slots.

## Property Reads

`Opcode::GetProperty` for ordinary objects should call `objectGetProperty`.

Shape-mode read:

```cpp
if (!object.dictionaryMode) {
  auto slot = object.shape->slots.find(name);
  if (slot != object.shape->slots.end()) {
    return object.slots[slot->second];
  }
  return Value::undefined();
}
```

Dictionary-mode read uses the fallback dictionary and returns `undefined` for missing properties.

Array, string, class, instance, method, static method, and `super` paths remain unchanged in this phase.

## Property Writes

`Opcode::SetProperty` for ordinary objects should call `objectSetProperty`.

Shape-mode write updates an existing slot or transitions to a new shape. Dictionary-mode write updates the dictionary.

The operation must keep the current expression result behavior: property assignment pushes the assigned value back onto the VM stack.

## Delete And Dictionary Fallback

`del(obj, key)` should not try to remove a property from a shape. Instead:

1. If the object is in shape mode, materialize `dictionary` from `shape->slots` and `slots`.
2. Set `dictionaryMode = true`.
3. Clear or leave `shape/slots` unused by all property helpers.
4. Erase the requested key from `dictionary`.

After dictionary fallback, the object stays in dictionary mode permanently. This keeps deletion semantics simple and mirrors the idea that highly dynamic objects are less optimized.

## Builtins

Update ordinary object handling in builtins:

- `len(object)` uses `objectPropertyCount`.
- `has(object, key)` uses `objectHasProperty`.
- `keys(object)` uses `objectKeys`.
- `del(object, key)` uses `objectDeleteProperty`.

Instance handling remains map-based in this phase.

## GC

Add `ObjType::Shape` and mark shape objects.

Ordinary object marking:

```cpp
markObject(object->shape);
for (const Value& slot : object->slots) {
  markValue(slot);
}
for (const auto& entry : object->dictionary) {
  markValue(entry.second);
}
```

Shape marking:

```cpp
markObject(shape->parent);
for (const auto& transition : shape->transitions) {
  markObject(transition.second);
}
```

Shape property names are C++ strings and do not need GC marking. Shape transitions must be marked so a live shape keeps its transition tree reachable. The VM root shape must also be part of the root set.

## Copy-Out Compatibility

`VM::copyOutValue` currently converts GC ordinary objects back into legacy `Value` object maps. It should use `objectKeys` and `objectGetProperty` to build the map rather than reading internal storage.

This preserves existing tests and external behavior while changing VM internals.

## Tests

Behavior tests:

- Object literal property reads still work.
- Dynamic property assignment still works.
- Reassigning an existing property updates the old value.
- Missing ordinary object property still returns `undefined`.
- Two objects with the same properties have independent values.
- `len`, `has`, `keys`, and `del` preserve existing behavior.
- After `del`, get/set/has/keys/len still work through dictionary mode.

Shape tests:

- Two objects that add `x` then `y` share the same final shape.
- Two objects that add `x` then `y` versus `y` then `x` do not share the same final shape.
- Reassigning an existing property does not change shape.
- Deleting a property switches that object to dictionary mode.

GC tests:

- Shape-mode object slots keep nested strings, arrays, objects, classes, and closures alive.
- Unreachable shape-mode objects are collected.
- Root object shape remains alive across manual GC.
- Dictionary-mode object values are marked after deletion fallback.

## Implementation Order

1. Add helper functions around the existing `unordered_map` implementation and update VM/builtins/copy-out call sites to use them.
2. Add `ObjShape`, `ObjType::Shape`, VM root shape allocation, and GC marking.
3. Change `ObjObject` to shape-mode fields plus dictionary fallback.
4. Update object literal creation and ordinary object get/set/delete helpers.
5. Add behavior tests and shape-sharing tests.
6. Add GC tests for slot marking and dictionary-mode marking.

Inline cache should only begin after this phase passes all existing tests and the new shape tests.

## Success Criteria

- All existing tests continue to pass.
- New object behavior tests pass.
- New shape-sharing tests pass.
- New GC tests pass.
- No CLI behavior changes are introduced.
- Ordinary object internals use shape/slots for non-deleted objects.
- `del` reliably switches ordinary objects to dictionary mode.

