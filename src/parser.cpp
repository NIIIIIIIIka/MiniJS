#include "minijs/parser.h"

#include "minijs/lexer.h"

#include <string>
#include <utility>

namespace minijs
{

Parser::Parser(std::string_view source)
{
    Lexer lexer(source);

    while (true)
    {
        Token token = lexer.nextToken();
        tokens_.push_back(token);

        if (token.type == TokenType::Eof)
        {
            break;
        }
    }

    diagnostics_ = lexer.diagnostics();
}

ExprPtr Parser::parse()
{
    ExprPtr result = expression();

    if (match(TokenType::Semicolon))
    {
        return result;
    }

    if (!isAtEnd())
    {
        report(peek(), "expected end of expression");
    }

    return result;
}

const std::vector<Diagnostic>& Parser::diagnostics() const
{
    return diagnostics_;
}

ExprPtr Parser::expression()
{
    return term();
}

ExprPtr Parser::term()
{
    // BinaryExpr(+ -) : (left_expr, op, right_expr)
    ExprPtr expr = factor();

    while (match(TokenType::Plus) || match(TokenType::Minus))
    {
        const TokenType op = previous().type;
        ExprPtr right = factor();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::factor()
{
    // BinaryExpr(* / %) : (left_expr, op, right_expr)
    ExprPtr expr = primary();

    while (match(TokenType::Star) || match(TokenType::Slash) || match(TokenType::Percent))
    {
        const TokenType op = previous().type;
        ExprPtr right = primary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
    }

    return expr;
}

ExprPtr Parser::primary()
{
    // NumberExpr : (number)
    if (match(TokenType::Number))
    {
        const Token& token = previous();
        return std::make_unique<NumberExpr>(std::string(token.lexeme));
    }

    // GroupingExpr : ('(', expr, ')')
    if (match(TokenType::LeftParen))
    {
        ExprPtr expr = expression();

        if (!match(TokenType::RightParen))
        {
            report(peek(), "expected ')' after expression");
        }

        return std::make_unique<GroupingExpr>(std::move(expr));
    }

    report(peek(), "expected expression");
    if (!isAtEnd())
    {
        advance();
    }
    return std::make_unique<NumberExpr>("0");
}

bool Parser::match(TokenType type)
{
    if (!check(type))
    {
        return false;
    }

    advance();
    return true;
}

bool Parser::check(TokenType type) const
{
    if (isAtEnd())
    {
        return type == TokenType::Eof;
    }

    return peek().type == type;
}

bool Parser::isAtEnd() const
{
    return peek().type == TokenType::Eof;
}

const Token& Parser::advance()
{
    if (!isAtEnd())
    {
        ++current_;
    }

    return previous();
}

const Token& Parser::peek() const
{
    return tokens_[current_];
}

const Token& Parser::previous() const
{
    return tokens_[current_ - 1];
}

void Parser::report(const Token& token, std::string message)
{
    diagnostics_.emplace_back(token.location, std::move(message));
}

}
