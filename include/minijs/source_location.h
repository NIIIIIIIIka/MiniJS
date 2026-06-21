#pragma once

#include <cstddef>

namespace minijs
{
struct SourceLocation
{
    std::size_t offset = 0;  // 在整个源文件中的字符偏移量（从0开始）
    std::size_t line = 1;    // 行号（从1开始，更符合人类阅读习惯）
    std::size_t column = 1;  // 列号（从1开始）
};

// 比较两个位置是否相同（用于测试或去重）
constexpr bool operator==(const SourceLocation& lhs, const SourceLocation& rhs)
{
    return lhs.offset == rhs.offset && lhs.line == rhs.line &&
           lhs.column == rhs.column;
}

constexpr bool operator!=(const SourceLocation& lhs, const SourceLocation& rhs)
{
    return !(lhs == rhs);
}
}
