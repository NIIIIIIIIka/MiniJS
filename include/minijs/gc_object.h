#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "minijs/object.h"
#include "minijs/value.h"

namespace minijs {

// VM GC 管理的数组对象，元素继续保存为动态 Value。
struct ObjArray final : public Obj {
  explicit ObjArray(std::vector<Value> elements);

  std::vector<Value> elements;
};

// VM GC 管理的对象字面量，属性值继续保存为动态 Value。
struct ObjObject final : public Obj {
  explicit ObjObject(std::unordered_map<std::string, Value> properties);

  std::unordered_map<std::string, Value> properties;
};

// VM GC 管理的字节码实例，字段保存在实例自身。
struct ObjInstance final : public Obj {
  explicit ObjInstance(std::shared_ptr<BytecodeClass> klass);

  std::shared_ptr<BytecodeClass> klass;
  std::unordered_map<std::string, Value> fields;
};

}  // namespace minijs
