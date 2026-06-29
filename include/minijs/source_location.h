#pragma once

#include <cstddef>

namespace minijs {

// 标识 token 或诊断在原始源码中的位置。
struct SourceLocation {
  // 从源码开头开始计算的零基字符偏移。
  std::size_t offset = 0;

  // 面向用户的一基行号。
  std::size_t line = 1;

  // 面向用户的一基列号。
  std::size_t column = 1;
};

// 判断两个源码位置是否完全相同。
constexpr bool operator==(const SourceLocation& lhs, const SourceLocation& rhs) {
  return lhs.offset == rhs.offset && lhs.line == rhs.line && lhs.column == rhs.column;
}

// 判断两个源码位置是否不同。
constexpr bool operator!=(const SourceLocation& lhs, const SourceLocation& rhs) {
  return !(lhs == rhs);
}

}  // namespace minijs
