#pragma once

#include <cstddef>
#include <string>
namespace minijs
{
struct SourceLocation
{
    std::size_t offset = 0;  // ������Դ�ļ��е��ַ�ƫ��������0��ʼ��
    std::size_t line = 1;    // �кţ���1��ʼ�������������Ķ�ϰ�ߣ�
    std::size_t column = 1;  // �кţ���1��ʼ��
};

// �Ƚ�����λ���Ƿ���ͬ�����ڲ��Ի�ȥ�أ�
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
