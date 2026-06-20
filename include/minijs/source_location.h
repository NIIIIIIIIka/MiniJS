#pragma once

#include <cstddef>

namespace minijs
{
struct SourceLocation
{
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

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
