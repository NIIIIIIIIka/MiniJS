#pragma once

#include "minijs/source_location.h"

#include <string_view>

namespace minijs
{

    // 词法分析中的Token类型枚举
    enum class TokenType
    {
        // -------- 单字符符号（语法分隔符） --------
        LeftParen,   // 左括号: (
        RightParen,  // 右括号: )
        LeftBrace,   // 左大括号: {
        RightBrace,  // 右大括号: }
        Comma,       // 逗号: ,
        Dot,         // 点号: . (可能用于对象属性访问)
        Semicolon,   // 分号: ; (语句结束符)

        // -------- 运算符（算术操作） --------
        Plus,        // 加号: +
        Minus,       // 减号: -
        Star,        // 星号: * (乘法或指针解引用)
        Slash,       // 斜杠: / (除法)

        // -------- 特殊Token --------
        Equal,       // 等号: = (赋值操作符)
        Identifier,  // 标识符: 变量名/函数名 (如: foo, bar, count)
        Number,      // 数字字面量: 整数或浮点数 (如: 42, 3.14)

        // -------- 关键字（保留字） --------
        Let,         // 关键字: let (用于变量声明，如: let x = 10;)

        // -------- 结束标记 --------
        Invalid,     // 无法识别的字符
        Eof          // 文件结束标记 (End Of File)，表示源码已全部解析完毕
    };

struct Token
{
    TokenType type;
    std::string_view lexeme;
    SourceLocation location;
};

}
