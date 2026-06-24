#pragma once

#include "minijs/ast.h"
#include "minijs/diagnostic.h"
#include "minijs/token.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace minijs
{

class Parser
{
public:
    explicit Parser(std::string_view source);

    ExprPtr parse();

    const std::vector<Diagnostic>& diagnostics() const;

private:
    ExprPtr expression();
    //最低优先级：加减（+ -）
    ExprPtr term();
    //中等优先级：乘除取模（* / %）
    ExprPtr factor();
    //最高优先级：字面量、括号
    ExprPtr primary();

    bool match(TokenType type);
    bool check(TokenType type) const;
    bool isAtEnd() const;
    const Token& advance();
    const Token& peek() const;
    const Token& previous() const;

    void report(const Token& token, std::string message);

    std::vector<Token> tokens_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t current_ = 0;
};

}
