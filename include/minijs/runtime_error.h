#pragma once

#include <stdexcept>
#include <string>

namespace minijs {

class RuntimeError final : public std::runtime_error {
 public:
  explicit RuntimeError(const std::string& message)
      : std::runtime_error("RuntimeError: " + message) {}
};

}  // namespace minijs