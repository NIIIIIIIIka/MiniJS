#include "minijs/interpreter.h"

#include <cmath>
#include <iostream>
#include <unordered_map>
#include <utility>

#include "minijs/runtime_error.h"

namespace minijs {
namespace {
class ReturnSignal {
 public:
  explicit ReturnSignal(Value value) : value_(value) {}

  const Value& value() const { return value_; }

 private:
  Value value_;
};

std::size_t checkedArrayIndex(const Value& index, std::size_t size) {
  if (!index.isNumber()) {
    throw RuntimeError("array index must be a non-negative integer");
  }

  const double number = index.asNumber();
  if (number < 0 || std::floor(number) != number) {
    throw RuntimeError("array index must be a non-negative integer");
  }

  const std::size_t offset = static_cast<std::size_t>(number);
  if (offset >= size) {
    throw RuntimeError("array index out of bounds");
  }

  return offset;
}
}  // namespace
Value Interpreter::interpret(const Program& program) {
  lastValue_ = Value();

  try {
    for (const StmtPtr& statement : program) {
      if (statement) {
        execute(*statement);
      }
    }
  } catch (const ReturnSignal&) {
    throw RuntimeError("return outside function");
  }

  return lastValue_;
}

Interpreter::Interpreter()
    : global_environment_(std::make_shared<Environment>()), environment_(global_environment_) {}

void Interpreter::execute(const Stmt& statement) {
  if (const auto* letStmt = dynamic_cast<const LetStmt*>(&statement)) {
    Value value = evaluate(letStmt->initializer());
    environment_->define(letStmt->name(), value);
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
    auto block_environment = std::make_shared<Environment>(environment_);
    executeBlock(blockStmt->statements(), block_environment);
    return;
  }

  if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&statement)) {
    while (evaluate(whileStmt->condition()).isTruthy()) {
      execute(whileStmt->body());
    }
    return;
  }

  if (const auto* functionStmt = dynamic_cast<const FunctionStmt*>(&statement)) {
    environment_->define(functionStmt->name(), Value(functionStmt, environment_));
    return;
  }

  if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&statement)) {
    if (returnStmt->value() == nullptr) {
      throw ReturnSignal(Value::undefined());
    }

    throw ReturnSignal(evaluate(*returnStmt->value()));
  }

  throw RuntimeError("unknown statement type");
}

Value Interpreter::evaluate(const Expr& expression) {
  if (dynamic_cast<const UndefinedExpr*>(&expression) != nullptr) {
    return Value::undefined();
  }

  if (const auto* number = dynamic_cast<const NumberExpr*>(&expression)) {
    return Value(std::stod(number->value()));
  }

  if (const auto* string = dynamic_cast<const StringExpr*>(&expression)) {
    return Value(string->value());
  }

  if (const auto* boolean = dynamic_cast<const BoolExpr*>(&expression)) {
    if (boolean->value() == "true") {
      return Value(true);
    } else if (boolean->value() == "false") {
      return Value(false);
    }
    throw RuntimeError("unsupported boolean expression");
  }

  if (dynamic_cast<const NullExpr*>(&expression) != nullptr) {
    return Value();
  }

  if (const auto* assign = dynamic_cast<const AssignExpr*>(&expression)) {
    Value value = evaluate(assign->value());
    environment_->assign(assign->name(), value);
    return value;
  }

  if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
    return environment_->get(variable->name());
  }

  if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression)) {
    return evaluate(grouping->expression());
  }

  if (const auto* array = dynamic_cast<const ArrayExpr*>(&expression)) {
    std::vector<Value> elements;
    for (const ExprPtr& element : array->elements()) {
      elements.push_back(evaluate(*element));
    }
    return Value(std::move(elements));
  }

  if (const auto* index = dynamic_cast<const IndexExpr*>(&expression)) {
    Value object = evaluate(index->object());
    Value indexValue = evaluate(index->index());
    const std::size_t offset = checkedArrayIndex(indexValue, object.asArray().size());
    return object.asArray()[offset];
  }

  if (const auto* indexAssign = dynamic_cast<const IndexAssignExpr*>(&expression)) {
    Value object = evaluate(indexAssign->object());
    Value indexValue = evaluate(indexAssign->index());
    Value value = evaluate(indexAssign->value());
    const std::size_t offset = checkedArrayIndex(indexValue, object.asArray().size());
    object.asArray()[offset] = value;
    return value;
  }
  if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
    Value right = evaluate(unary->right());
    switch (unary->op()) {
      case (TokenType::Bang):
        return Value(!right.isTruthy());
      case (TokenType::Minus):
        if (right.isNumber()) return Value(-right.asNumber());
        throw RuntimeError("unsupported object to use '-'");
      default:
        throw RuntimeError("unsupported unary operator");
    }
  }

  if (const auto* logical = dynamic_cast<const LogicalExpr*>(&expression)) {
    Value left = evaluate(logical->left());

    if (logical->op() == TokenType::OrOr) {
      if (left.isTruthy()) {
        return left;
      }
      return evaluate(logical->right());
    }

    if (logical->op() == TokenType::AndAnd) {
      if (!left.isTruthy()) {
        return left;
      }
      return evaluate(logical->right());
    }

    throw RuntimeError("unsupported logical operator");
  }
  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
    const Value left = evaluate(binary->left());
    const Value right = evaluate(binary->right());

    switch (binary->op()) {
      case TokenType::Plus: {
        if (left.isString() || right.isString()) {
          return Value(left.toString() + right.toString());
        }
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs + rhs);
      }
      case TokenType::Minus: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs - rhs);
      }
      case TokenType::Star: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs * rhs);
      }
      case TokenType::Slash: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        if (rhs == 0) {
          throw RuntimeError("division by zero");
        }
        return Value(lhs / rhs);
      }
      case TokenType::Percent: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        if (rhs == 0) {
          throw RuntimeError("modulo by zero");
        }
        return Value(std::fmod(lhs, rhs));
      }
      case TokenType::Greater: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs > rhs);
      }
      case TokenType::GreaterEqual: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs >= rhs);
      }
      case TokenType::Less: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs < rhs);
      }
      case TokenType::LessEqual: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs <= rhs);
      }
      case TokenType::EqualEqual: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs == rhs);
      }
      case TokenType::BangEqual: {
        const double lhs = left.asNumber();
        const double rhs = right.asNumber();
        return Value(lhs != rhs);
      }
      default:
        throw RuntimeError("unsupported binary operator");
    }
  }
  if (const auto* call = dynamic_cast<const CallExpr*>(&expression)) {
    if (call->callee() == "print") {
      if (call->arguments().size() != 1) {
        throw RuntimeError("print expects 1 argument");
      }
      Value value = evaluate(*call->arguments()[0]);
      std::cout << value.toString() << '\n';
      return Value();
    }

    Value callee;
    try {
      callee = environment_->get(call->callee());
    } catch (const RuntimeError&) {
      throw RuntimeError("undefined function: " + call->callee());
    }

    if (!callee.isFunction()) {
      throw RuntimeError(call->callee() + " is not callable");
    }

    const FunctionValue& function = callee.asFunction();
    const FunctionStmt* declaration = function.declaration;

    if (declaration == nullptr) {
      throw RuntimeError(call->callee() + " is not callable");
    }

    if (call->arguments().size() != declaration->params().size()) {
      throw RuntimeError("function " + call->callee() + " expects " +
                         std::to_string(declaration->params().size()) + " arguments");
    }

    auto call_environment = std::make_shared<Environment>(function.closure);
    for (std::size_t i = 0; i < declaration->params().size(); ++i) {
      (*call_environment.get()).define(declaration->params()[i], evaluate(*call->arguments()[i]));
    }

    try {
      executeBlock(declaration->body(), call_environment);
      return Value::undefined();
    } catch (const ReturnSignal& signal) {
      return signal.value();
    }
  }

  if (const auto* call = dynamic_cast<const MethodCallExpr*>(&expression)) {
    Value object = evaluate(call->object());
    if (object.isArray() && call->name() == "push") {
      if (call->arguments().size() != 1) {
        throw RuntimeError("array.push expects 1 argument");
      }

      Value value = evaluate(*call->arguments()[0]);
      object.asArray().push_back(value);
      return Value(static_cast<double>(object.asArray().size()));
    }
    if (object.isArray() && call->name() == "pop") {
      if (!call->arguments().empty()) {
        throw RuntimeError("array.pop expects 0 arguments");
      }

      auto& array = object.asArray();

      if (array.empty()) {
        return Value::undefined();
      }

      Value value = array.back();
      array.pop_back();
      return value;
    }
    if (object.isArray()) {
      throw RuntimeError("undefined method: " + call->name());
    }
    throw RuntimeError("value has no method: " + call->name());
  }
  if (const auto* object = dynamic_cast<const ObjectExpr*>(&expression)) {
    std::unordered_map<std::string, Value> properties;
    for (const auto& property : object->properties()) {
      properties[property.name] = evaluate(*property.value);
    }
    return Value(std::move(properties));
  }

  if (const auto* get = dynamic_cast<const GetExpr*>(&expression)) {
    Value object = evaluate(get->object());

    if (object.isArray() && get->name() == "length") {
      return Value(static_cast<double>(object.asArray().size()));
    }

    if (object.isString() && get->name() == "length") {
      return Value(static_cast<double>(object.asString().size()));
    }

    const auto& properties = object.asObject();
    auto it = properties.find(get->name());
    if (it == properties.end()) {
      throw RuntimeError("undefined property: " + get->name());
    }

    return it->second;
  }

  if (const auto* set = dynamic_cast<const SetExpr*>(&expression)) {
    Value object = evaluate(set->object());
    Value value = evaluate(set->value());

    object.asObject()[set->name()] = value;
    return value;
  }
  throw RuntimeError("unknown expression type");
}

void Interpreter::executeBlock(const Program& statements,
                               std::shared_ptr<Environment> environment) {
  auto previous = environment_;
  environment_ = environment;

  try {
    for (const StmtPtr& inner : statements) {
      if (inner) {
        execute(*inner);
      }
    }
  } catch (...) {
    environment_ = previous;
    throw;
  }

  environment_ = previous;
}

}  // namespace minijs
