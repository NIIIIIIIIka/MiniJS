#pragma once

#include "minijs/token.h"

#include <memory>
#include <string>

namespace minijs
{

class Expr
{
public:
    virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

class NumberExpr final : public Expr
{
public:
    explicit NumberExpr(std::string value);

    const std::string& value() const;

private:
    std::string value_;
};

class BinaryExpr final : public Expr
{
public:
    BinaryExpr(ExprPtr left, TokenType op, ExprPtr right);

    const Expr& left() const;
    TokenType op() const;
    const Expr& right() const;

private:
    ExprPtr left_;
    TokenType op_;
    ExprPtr right_;
};

class GroupingExpr final : public Expr
{
public:
    explicit GroupingExpr(ExprPtr expression);

    const Expr& expression() const;

private:
    ExprPtr expression_;
};

std::string formatExpr(const Expr& expression);

}
