#include "minijs/interpreter.h"

#include <cmath>

#include "minijs/runtime_error.h"

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

  if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&statement)) {
    if (evaluate(ifStmt->condition()).isTruthy()) {
      execute(ifStmt->thenBranch());
    } else if (ifStmt->elseBranch() != nullptr) {
      execute(*ifStmt->elseBranch());
    }
    return;
  }

  if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(&statement)) {
    lastValue_ = evaluate(exprStmt->expression());
    return;
  }

  if (const auto* blockStmt = dynamic_cast<const BlockStmt*>(&statement)) {
    for (const StmtPtr& inner : blockStmt->statements()) {
      if (inner) {
        execute(*inner);
      }
    }
    return;
  }
  if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&statement)) {
    while (evaluate(whileStmt->condition()).isTruthy()) {
      execute(whileStmt->body());
    }
    return;
  }
  throw RuntimeError("unknown statement type");
}

Value Interpreter::evaluate(const Expr& expression) {
  if (const auto* number = dynamic_cast<const NumberExpr*>(&expression)) {
    return Value(std::stod(number->value()));
  }
  if (const auto* assign = dynamic_cast<const AssignExpr*>(&expression)) {
    Value value = evaluate(assign->value());
    environment_.assign(assign->name(), value);
    return value;
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
        if (rhs == 0) {
          throw RuntimeError("division by zero");
        }
        return Value(lhs / rhs);
      case TokenType::Percent:
        if (rhs == 0) {
          throw RuntimeError("modulo by zero");
        }
        return Value(std::fmod(lhs, rhs));
      case TokenType::Greater:
        return Value(lhs > rhs);
      case TokenType::GreaterEqual:
        return Value(lhs >= rhs);
      case TokenType::Less:
        return Value(lhs < rhs);
      case TokenType::LessEqual:
        return Value(lhs <= rhs);
      case TokenType::EqualEqual:
        return Value(lhs == rhs);
      case TokenType::BangEqual:
        return Value(lhs != rhs);
      default:
        throw RuntimeError("unsupported binary operator");
    }
  }

  throw RuntimeError("unknown expression type");
}

}  // namespace minijs
