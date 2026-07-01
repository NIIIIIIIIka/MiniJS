#include "minijs/environment.h"

#include <utility>

#include "minijs/runtime_error.h"

namespace minijs {
Environment::Environment() {}
Environment::Environment(std::shared_ptr<Environment> parent) : parent_(std::move(parent)) {}
void Environment::define(std::string name, Value value) { values_[std::move(name)] = value; }

Value Environment::get(const std::string& name) const {
  const auto it = values_.find(name);
  if (it != values_.end()) {
    return it->second;
  } else if (parent_) {
    return parent_->get(name);
  }
  throw RuntimeError("undefined variable: " + name);
}

void Environment::assign(const std::string& name, Value value) {
  const auto it = values_.find(name);
  if (it != values_.end()) {
    it->second = value;
    return;
  } else if (parent_) {
    parent_->assign(name, value);
    return;
  }
  throw RuntimeError("undefined variable: " + name);
}
}  // namespace minijs
