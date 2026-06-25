#include "minijs/environment.h"

#include <stdexcept>
#include <utility>

namespace minijs {

void Environment::define(std::string name, Value value) { values_[std::move(name)] = value; }

Value Environment::get(const std::string& name) const {
  const auto it = values_.find(name);
  if (it == values_.end()) {
    throw std::runtime_error("undefined variable: " + name);
  }

  return it->second;
}

}  // namespace minijs
