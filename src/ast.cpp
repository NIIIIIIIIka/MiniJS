#include "minijs/ast.h"

#include <stdexcept>
#include <utility>

namespace minijs
{
namespace
{
std::string binaryOpName(TokenType type)
{
    switch (type)
    {
    case TokenType::Plus:
        return "+";
    case TokenType::Minus:
        return "-";
    case TokenType::Star:
        return "*";
    case TokenType::Slash:
        return "/";
    case TokenType::Percent:
        return "%";
    default:
        throw std::logic_error("unsupported binary operator");
    }
}
}

NumberExpr::NumberExpr(std::string value)
    : value_(std::move(value))
{
}

const std::string& NumberExpr::value() const
{
    return value_;
}

BinaryExpr::BinaryExpr(ExprPtr left, TokenType op, ExprPtr right)
    : left_(std::move(left)), op_(op), right_(std::move(right))
{
}

const Expr& BinaryExpr::left() const
{
    return *left_;
}

TokenType BinaryExpr::op() const
{
    return op_;
}

const Expr& BinaryExpr::right() const
{
    return *right_;
}

GroupingExpr::GroupingExpr(ExprPtr expression)
    : expression_(std::move(expression))
{
}

const Expr& GroupingExpr::expression() const
{
    return *expression_;
}

std::string formatExpr(const Expr& expression)
{
    if (const auto* number = dynamic_cast<const NumberExpr*>(&expression))
    {
        return number->value();
    }

    if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression))
    {
        return "(" + binaryOpName(binary->op()) + " " + formatExpr(binary->left()) +
               " " + formatExpr(binary->right()) + ")";
    }

    if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression))
    {
        return "(group " + formatExpr(grouping->expression()) + ")";
    }

    throw std::logic_error("unknown expression type");
}

}
