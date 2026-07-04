#pragma once

#include <string>
#include <unordered_map>
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
  const Value& peek() const;

  std::vector<Value> stack_;
  std::unordered_map<std::string, Value> globals_;
};

}  // namespace minijs
