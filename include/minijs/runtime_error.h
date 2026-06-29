#pragma once

#include <stdexcept>
#include <string>

namespace minijs {

// MiniJS 运行期错误，用于区别解释执行错误和宿主程序错误。
class RuntimeError final : public std::runtime_error {
 public:
  explicit RuntimeError(const std::string& message)
      : std::runtime_error("RuntimeError: " + message) {}
};

}  // namespace minijs
