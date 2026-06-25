#pragma once

#include <cstddef>

namespace minijs {
struct SourceLocation {
  std::size_t offset = 0;  // 源文件中的字符偏移，从 0 开始。
  std::size_t line = 1;    // 行号，从 1 开始。
  std::size_t column = 1;  // 列号，从 1 开始。
};

constexpr bool operator==(const SourceLocation& lhs, const SourceLocation& rhs) {
  return lhs.offset == rhs.offset && lhs.line == rhs.line && lhs.column == rhs.column;
}

constexpr bool operator!=(const SourceLocation& lhs, const SourceLocation& rhs) {
  return !(lhs == rhs);
}
}  // namespace minijs
