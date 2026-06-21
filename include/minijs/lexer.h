#pragma once

#include "minijs/source_location.h"
#include "minijs/token.h"

#include <cstddef>
#include <string_view>

namespace minijs
{

//词法分析器
class Lexer
{
public:
    explicit Lexer(std::string_view source);
    Token next();

private:
    /**
     * 判断：是否已经到达字符串末尾
     * @return 两数之和
    */
    bool isAtEnd() const;

    /**
    * 读一个字符，并把 current_  往前推，更新location_
    * @return 下一个字符
    */  
    char advance();

    /**
    * 看当前字符，但不前进
    * @return 当前字符
    */
    char peek() const;

    /**
    * 跳过空白符
    */
    void skipWhitespace();

    /**
    * 创建 Token
    * @type 类型
    * @start 在字符串中该Token起始地址
    * @startLocation 起始地址
    * @return 创建的Token
    */
    Token makeToken(TokenType type,std::size_t start,SourceLocation startLocation) const;

    Token scanIdentifier(std::size_t start, SourceLocation startLocation);

    Token scanNumber(std::size_t start, SourceLocation startLocation);


    //判断是否为变量类型的开头
    static bool isIdentifierStart(char ch);

    //判断是否为变量类型的部分
    static bool isIdentifierPart(char ch);

    static bool isDigit(char ch);

    std::string_view source_; // 
    std::size_t current_ = 0; // 始终指向“下一个尚未读取的字符”。
    SourceLocation location_; // 
};

}
