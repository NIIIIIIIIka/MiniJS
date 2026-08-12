#include "minijs/gc_object.h"

#include <utility>

namespace minijs {

ObjArray::ObjArray(std::vector<Value> elements)
    : Obj(ObjType::Array), elements(std::move(elements)) {}

ObjShape::ObjShape(ObjShape* parent, std::string addedProperty)
    : Obj(ObjType::Shape), parent(parent), addedProperty(std::move(addedProperty)) {}

ObjObject::ObjObject(ObjShape* rootShape) : Obj(ObjType::Object), shape(rootShape) {}

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
