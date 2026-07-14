#include "minijs/object.h"

#include <utility>

namespace minijs {

Obj::Obj(ObjType type) : type(type) {}

ObjString::ObjString(std::string value) : Obj(ObjType::String), value(std::move(value)) {}
}  // namespace minijs
