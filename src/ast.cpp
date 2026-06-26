#include "minijs/ast.h"

#include <stdexcept>
#include <utility>

namespace minijs {
namespace {
std::string binaryOpName(TokenType type) {
  switch (type) {
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
    case TokenType::Greater:
      return ">";
    case TokenType::GreaterEqual:
      return ">=";
    case TokenType::Less:
      return "<";
    case TokenType::LessEqual:
      return "<=";
    case TokenType::EqualEqual:
      return "==";
    case TokenType::BangEqual:
      return "!=";
    default:
      throw std::logic_error("unsupported binary operator");
  }
}
}  // namespace

NumberExpr::NumberExpr(std::string value) : value_(std::move(value)) {}

const std::string& NumberExpr::value() const { return value_; }

BinaryExpr::BinaryExpr(ExprPtr left, TokenType op, ExprPtr right)
    : left_(std::move(left)), op_(op), right_(std::move(right)) {}

const Expr& BinaryExpr::left() const { return *left_; }

TokenType BinaryExpr::op() const { return op_; }

const Expr& BinaryExpr::right() const { return *right_; }

GroupingExpr::GroupingExpr(ExprPtr expression) : expression_(std::move(expression)) {}

const Expr& GroupingExpr::expression() const { return *expression_; }

VariableExpr::VariableExpr(std::string name) : name_(std::move(name)) {}

const std::string& VariableExpr::name() const { return name_; }

ExprStmt::ExprStmt(ExprPtr expression) : expression_(std::move(expression)) {}

const Expr& ExprStmt::expression() const { return *expression_; }

LetStmt::LetStmt(std::string name, ExprPtr initializer)
    : name_(std::move(name)), initializer_(std::move(initializer)) {}

const std::string& LetStmt::name() const { return name_; }

const Expr& LetStmt::initializer() const { return *initializer_; }

std::string formatExpr(const Expr& expression) {
  if (const auto* number = dynamic_cast<const NumberExpr*>(&expression)) {
    return number->value();
  }

  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
    return "(" + binaryOpName(binary->op()) + " " + formatExpr(binary->left()) + " " +
           formatExpr(binary->right()) + ")";
  }

  if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression)) {
    return "(group " + formatExpr(grouping->expression()) + ")";
  }

  if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
    return variable->name();
  }

  throw std::logic_error("unknown expression type");
}

std::string formatStmt(const Stmt& statement) {
  if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(&statement)) {
    return "(expr " + formatExpr(exprStmt->expression()) + ")";
  }

  if (const auto* letStmt = dynamic_cast<const LetStmt*>(&statement)) {
    return "(let " + letStmt->name() + " " + formatExpr(letStmt->initializer()) + ")";
  }

  if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&statement)) {
    std::string result =
        "(if " + formatExpr(ifStmt->condition()) + " " + formatStmt(ifStmt->thenBranch());

    if (ifStmt->elseBranch() != nullptr) {
      result += " " + formatStmt(*ifStmt->elseBranch());
    }

    result += ")";
    return result;
  }
  throw std::logic_error("unknown statement type");
}

std::string formatProgram(const Program& program) {
  std::string result;

  for (const StmtPtr& statement : program) {
    if (!statement) {
      continue;
    }

    if (!result.empty()) {
      result += '\n';
    }

    result += formatStmt(*statement);
  }

  return result;
}

IfStmt::IfStmt(ExprPtr condition, StmtPtr thenBranch, StmtPtr elseBranch)
    : condition_(std::move(condition)),
      thenBranch_(std::move(thenBranch)),
      elseBranch_(std::move(elseBranch)) {}

const Expr& IfStmt::condition() const { return *condition_; }

const Stmt& IfStmt::thenBranch() const { return *thenBranch_; }

const Stmt* IfStmt::elseBranch() const { return elseBranch_.get(); }

}  // namespace minijs
