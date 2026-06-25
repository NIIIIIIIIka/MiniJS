#include "minijs/interpreter.h"

#include <cmath>
#include <stdexcept>

namespace minijs {

Value Interpreter::interpret(const Program& program) {
  lastValue_ = Value();

  for (const StmtPtr& statement : program) {
    if (statement) {
      execute(*statement);
    }
  }

  return lastValue_;
}

void Interpreter::execute(const Stmt& statement) {
  if (const auto* letStmt = dynamic_cast<const LetStmt*>(&statement)) {
    Value value = evaluate(letStmt->initializer());
    environment_.define(letStmt->name(), value);
    return;
  }

  if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(&statement)) {
    lastValue_ = evaluate(exprStmt->expression());
    return;
  }

  throw std::runtime_error("unknown statement type");
}

Value Interpreter::evaluate(const Expr& expression) {
  if (const auto* number = dynamic_cast<const NumberExpr*>(&expression)) {
    return Value(std::stod(number->value()));
  }

  if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
    return environment_.get(variable->name());
  }

  if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression)) {
    return evaluate(grouping->expression());
  }

  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
    const Value left = evaluate(binary->left());
    const Value right = evaluate(binary->right());

    const double lhs = left.asNumber();
    const double rhs = right.asNumber();

    switch (binary->op()) {
      case TokenType::Plus:
        return Value(lhs + rhs);
      case TokenType::Minus:
        return Value(lhs - rhs);
      case TokenType::Star:
        return Value(lhs * rhs);
      case TokenType::Slash:
        return Value(lhs / rhs);
      case TokenType::Percent:
        return Value(std::fmod(lhs, rhs));
      default:
        throw std::runtime_error("unsupported binary operator");
    }
  }

  throw std::runtime_error("unknown expression type");
}

}  // namespace minijs
