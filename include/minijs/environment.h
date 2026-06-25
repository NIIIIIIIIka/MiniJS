#pragma once

#include <string>
#include <unordered_map>

#include "minijs/value.h"

namespace minijs {

class Environment {
 public:
  void define(std::string name, Value value);
  Value get(const std::string& name) const;

 private:
  std::unordered_map<std::string, Value> values_;
};

}  // namespace minijs
