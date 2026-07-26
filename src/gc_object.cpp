#include "minijs/gc_object.h"

#include <utility>

namespace minijs {

ObjArray::ObjArray(std::vector<Value> elements)
    : Obj(ObjType::Array), elements(std::move(elements)) {}

ObjObject::ObjObject(std::unordered_map<std::string, Value> properties)
    : Obj(ObjType::Object), properties(std::move(properties)) {}

ObjInstance::ObjInstance(ObjClass* klass) : Obj(ObjType::Instance), klass(klass) {}

ObjBoundMethod::ObjBoundMethod(Value receiver, std::shared_ptr<BytecodeClosure> method)
    : Obj(ObjType::BoundMethod), receiver(std::move(receiver)), method(std::move(method)) {}

ObjClass::ObjClass(std::string name) : Obj(ObjType::Class), name(std::move(name)) {}
}  // namespace minijs
