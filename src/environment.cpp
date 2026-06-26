#include "minijs/environment.h"

#include <utility>

#include "minijs/runtime_error.h"

namespace minijs {

void Environment::define(std::string name, Value value) { values_[std::move(name)] = value; }

Value Environment::get(const std::string& name) const {
  const auto it = values_.find(name);
  if (it == values_.end()) {
    throw RuntimeError("undefined variable: " + name);
  }

  return it->second;
}

void Environment::assign(const std::string& name, Value value) {
  const auto it = values_.find(name);
  if (it == values_.end()) {
    throw RuntimeError("undefined variable: " + name);
  }

  it->second = value;
}
}  // namespace minijs
