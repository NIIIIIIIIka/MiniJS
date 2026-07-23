#include "minijs/gc_object.h"

#include <utility>

namespace minijs {

ObjArray::ObjArray(std::vector<Value> elements)
    : Obj(ObjType::Array), elements(std::move(elements)) {}

ObjObject::ObjObject(std::unordered_map<std::string, Value> properties)
    : Obj(ObjType::Object), properties(std::move(properties)) {}

ObjInstance::ObjInstance(std::shared_ptr<BytecodeClass> klass)
    : Obj(ObjType::Instance), klass(std::move(klass)) {}

}  // namespace minijs
