#include "minijs/gc_object.h"

#include <utility>

namespace minijs {

ObjArray::ObjArray(std::vector<Value> elements)
    : Obj(ObjType::Array), elements(std::move(elements)) {}

ObjObject::ObjObject(std::unordered_map<std::string, Value> properties)
    : Obj(ObjType::Object), properties(std::move(properties)) {}

ObjInstance::ObjInstance(ObjClass* klass) : Obj(ObjType::Instance), klass(klass) {}

ObjBoundMethod::ObjBoundMethod(Value receiver, ObjClosure* method)
    : Obj(ObjType::BoundMethod), receiver(std::move(receiver)), method(std::move(method)) {}

ObjClosure::ObjClosure(ObjFunction* function) : Obj(ObjType::Closure), function(function) {}

ObjClass::ObjClass(std::string name) : Obj(ObjType::Class), name(std::move(name)) {}

ObjUpvalue::ObjUpvalue(std::size_t stackIndex)
    : Obj(ObjType::Upvalue), stackIndex(stackIndex) {}

ObjFunction::ObjFunction(BytecodeFunction function)
    : Obj(ObjType::Function), function(std::move(function)) {}

ObjNativeFunction::ObjNativeFunction(std::string name, std::size_t arity, NativeFn function)
    : Obj(ObjType::NativeFunction),
      name(std::move(name)),
      arity(arity),
      function(std::move(function)) {}
}  // namespace minijs
