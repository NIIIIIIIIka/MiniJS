#pragma once

#include <vector>

#include "minijs/object.h"
#include "minijs/value.h"

namespace minijs {

// VM GC 管理的数组对象，元素继续保存为动态 Value。
struct ObjArray final : public Obj {
  explicit ObjArray(std::vector<Value> elements);

  std::vector<Value> elements;
};

}  // namespace minijs
