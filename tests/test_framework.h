#pragma once

#include <iostream>

namespace minijs::test
{
inline int failures = 0;

inline void expect(bool condition, const char* expression, const char* file, int line)
{
    if (condition)
    {
        return;
    }

    std::cerr << file << ':' << line << ": test failure: " << expression << '\n';
    ++failures;
}
}

#define EXPECT(expression) \
    ::minijs::test::expect((expression), #expression, __FILE__, __LINE__)
