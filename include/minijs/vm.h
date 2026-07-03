#pragma once

#include <vector>

#include "minijs/chunk.h"
#include "minijs/value.h"

namespace minijs {

class VM {
 public:
  Value run(const Chunk& chunk);

 private:
  void push(Value value);
  Value pop();

  std::vector<Value> stack_;
};

}  // namespace minijs
