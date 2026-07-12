#include "minijs/interpreter.h"

#include <cmath>
#include <iostream>
#include <unordered_map>
#include <utility>

#include "minijs/runtime_error.h"

namespace minijs {
namespace {
Value callArrayMethod(Value object, const std::string& name, const std::vector<Value>& arguments) {
  if (name == "push") {
    if (arguments.size() != 1) {
      throw RuntimeError("array.push expects 1 argument");
    }

    object.asArray().push_back(arguments[0]);
    return Value(static_cast<double>(object.asArray().size()));
  }

  if (name == "pop") {
    if (!arguments.empty()) {
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

  throw RuntimeError("undefined method: " + name);
}

const FunctionValue* findMethod(const std::shared_ptr<ClassValue>& klass, const std::string& name) {
  for (auto current = klass; current != nullptr; current = current->superclass) {
    const auto method = current->methods.find(name);
    if (method != current->methods.end()) {
      return &method->second;
    }
  }
  return nullptr;
}

class ReturnSignal {
 public:
  explicit ReturnSignal(Value value) : value_(value) {}

  const Value& value() const { return value_; }

 private:
  Value value_;
};

class ContinueSignal {};
class BreakSignal {};

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

void expectArity(const std::vector<Value>& arguments, std::size_t expected,
                 const std::string& name) {
  if (arguments.size() != expected) {
    throw RuntimeError(name + " expects " + std::to_string(expected) + " argument" +
                       (expected == 1 ? "" : "s"));
  }
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
  } catch (const BreakSignal&) {
    throw RuntimeError("break outside loop");
  } catch (const ContinueSignal&) {
    throw RuntimeError("continue outside loop");
  }

  return lastValue_;
}

Interpreter::Interpreter()
    : global_environment_(std::make_shared<Environment>()), environment_(global_environment_) {
  defineBuiltins();
}

void Interpreter::defineBuiltin(std::string name, BuiltinFunction function) {
  global_environment_->define(std::move(name), Value(std::move(function)));
}

void Interpreter::defineBuiltins() {
  defineBuiltin("print", [](const std::vector<Value>& arguments) -> Value {
    expectArity(arguments, 1, "print");

    std::cout << arguments[0].toString() << '\n';
    return Value();
  });

  defineBuiltin("has", [](const std::vector<Value>& arguments) -> Value {
    expectArity(arguments, 2, "has");

    const Value& object = arguments[0];
    const Value& key = arguments[1];

    if (!key.isString()) {
      throw RuntimeError("has key must be a string");
    }

    const std::string& name = key.asString();

    if (object.isObject()) {
      return Value(object.asObject().find(name) != object.asObject().end());
    }

    if (object.isArray()) {
      return Value(name == "length" || name == "push" || name == "pop");
    }

    if (object.isString()) {
      return Value(name == "length");
    }
    return Value(false);
  });

  defineBuiltin("keys", [](const std::vector<Value>& arguments) -> Value {
    expectArity(arguments, 1, "keys");

    const Value& object = arguments[0];
    std::vector<Value> keys;

    if (object.isObject()) {
      for (const auto& key : object.asObject()) {
        keys.push_back(Value(key.first));
      }
      return Value(std::move(keys));
    }
    if (object.isArray()) {
      keys.push_back(Value(std::string("length")));
      keys.push_back(Value(std::string("push")));
      keys.push_back(Value(std::string("pop")));
      return Value(std::move(keys));
    }

    if (object.isString()) {
      keys.push_back(Value(std::string("length")));
      return Value(std::move(keys));
    }

    return Value(std::move(keys));
  });

  defineBuiltin("del", [](const std::vector<Value>& arguments) -> Value {
    expectArity(arguments, 2, "del");

    Value object = arguments[0];
    const Value& key = arguments[1];

    if (!key.isString()) {
      throw RuntimeError("del key must be a string");
    }

    if (!object.isObject()) {
      return Value(false);
    }

    const std::string& name = key.asString();
    auto& properties = object.asObject();
    return Value(properties.erase(name) > 0);
  });

  defineBuiltin("typeOf", [](const std::vector<Value>& arguments) -> Value {
    expectArity(arguments, 1, "typeOf");

    const Value& value = arguments[0];
    if (value.isNumber()) {
      return Value(std::string("number"));
    }
    if (value.isBoolean()) {
      return Value(std::string("boolean"));
    }
    if (value.isString()) {
      return Value(std::string("string"));
    }
    if (value.isArray()) {
      return Value(std::string("array"));
    }
    if (value.isObject()) {
      return Value(std::string("object"));
    }
    if (value.isFunction()) {
      return Value(std::string("function"));
    }
    if (value.isBuiltinFunction()) {
      return Value(std::string("builtin"));
    }
    if (value.isNull()) {
      return Value(std::string("null"));
    }
    if (value.isUndefined()) {
      return Value(std::string("undefined"));
    }

    return Value(std::string("unknown"));
  });

  defineBuiltin("len", [](const std::vector<Value>& arguments) -> Value {
    expectArity(arguments, 1, "len");

    const Value& value = arguments[0];
    if (value.isString()) {
      return Value(static_cast<double>(value.asString().size()));
    }
    if (value.isArray()) {
      return Value(static_cast<double>(value.asArray().size()));
    }
    if (value.isObject()) {
      return Value(static_cast<double>(value.asObject().size()));
    }

    throw RuntimeError("value has no length");
  });
}

void Interpreter::execute(const Stmt& statement) {
  if (const auto* letStmt = dynamic_cast<const LetStmt*>(&statement)) {
    Value value = evaluate(letStmt->initializer());
    environment_->define(letStmt->name(), value);
    return;
  }
  if (const auto* classStmt = dynamic_cast<const ClassStmt*>(&statement)) {
    std::shared_ptr<ClassValue> superclass;
    if (classStmt->superclass().has_value()) {
      Value value = environment_->get(*classStmt->superclass());
      if (!value.isClass()) {
        throw RuntimeError("superclass must be a class");
      }
      superclass = value.asClass();
    }

    auto klass = std::make_shared<ClassValue>();
    klass->name = classStmt->name();
    klass->superclass = superclass;

    auto method_environment = environment_;
    if (superclass != nullptr) {
      // super is lexical: methods remember the superclass from this class declaration.
      // this is still supplied later by the receiver when the method is called.
      method_environment = std::make_shared<Environment>(environment_);
      method_environment->define("super", Value(superclass));
    }

    for (const auto& method : classStmt->methods()) {
      if (method) {
        klass->methods[method->name()] = FunctionValue{method.get(), method_environment};
      }
    }

    environment_->define(classStmt->name(), Value(klass));
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
      try {
        execute(whileStmt->body());
      } catch (const ContinueSignal&) {
        continue;
      } catch (const BreakSignal&) {
        break;
      }
    }
    return;
  }

  if (const auto* forStmt = dynamic_cast<const ForStmt*>(&statement)) {
    auto loop_environment = std::make_shared<Environment>(environment_);
    auto previous = environment_;
    environment_ = loop_environment;

    try {
      if (forStmt->initializer() != nullptr) {
        execute(*forStmt->initializer());
      }

      while (forStmt->condition() == nullptr || evaluate(*forStmt->condition()).isTruthy()) {
        bool should_break = false;
        try {
          execute(forStmt->body());
        } catch (const ContinueSignal&) {
          // 继续执行 increment
        } catch (const BreakSignal&) {
          should_break = true;
        }

        if (should_break) {
          break;
        }

        if (forStmt->increment() != nullptr) {
          evaluate(*forStmt->increment());
        }
      }
    } catch (...) {
      environment_ = previous;
      throw;
    }

    environment_ = previous;
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

  if (dynamic_cast<const BreakStmt*>(&statement) != nullptr) {
    throw BreakSignal();
  }

  if (dynamic_cast<const ContinueStmt*>(&statement) != nullptr) {
    throw ContinueSignal();
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
    try {
      return environment_->get(variable->name());
    } catch (const RuntimeError&) {
      if (variable->name() == "this") {
        throw RuntimeError("this outside method");
      }
      throw;
    }
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
        return Value(left.equals(right));
      }
      case TokenType::BangEqual: {
        return Value(!left.equals(right));
      }
      default:
        throw RuntimeError("unsupported binary operator");
    }
  }
  if (const auto* call = dynamic_cast<const CallExpr*>(&expression)) {
    const auto* callee_variable = dynamic_cast<const VariableExpr*>(&call->callee());

    Value callee;
    try {
      callee = evaluate(call->callee());
    } catch (const RuntimeError&) {
      if (callee_variable != nullptr) {
        throw RuntimeError("undefined function: " + callee_variable->name());
      }
      throw;
    }
    std::vector<Value> arguments;
    for (const ExprPtr& argument : call->arguments()) {
      arguments.push_back(evaluate(*argument));
    }

    if (callee.isBuiltinFunction()) {
      const BuiltinFunction& builtinFunction = callee.asBuiltinFunction();
      return builtinFunction(arguments);
    }

    if (callee.isClass()) {
      auto klass = callee.asClass();

      auto instance = std::make_shared<InstanceValue>();
      instance->klass = klass;
      const FunctionValue* init = findMethod(klass, "init");
      if (init == nullptr) {
        if (!arguments.empty()) {
          throw RuntimeError("class " + klass->name + " expects 0 arguments");
        }
        return Value(instance);
      }
      callMethod(instance, *init, arguments);
      return Value(instance);
    }

    if (callee.isBoundMethod()) {
      const auto boundMethod = callee.asBoundMethod();
      return callMethod(boundMethod->receiver, boundMethod->method, arguments);
    }

    if (callee.isFunction()) {
      const FunctionValue& function = callee.asFunction();
      const FunctionStmt* declaration = function.declaration;

      if (declaration == nullptr) {
        if (callee_variable != nullptr) {
          throw RuntimeError(callee_variable->name() + " is not callable");
        }
        throw RuntimeError("value is not callable");
      }

      if (call->arguments().size() != declaration->params().size()) {
        throw RuntimeError("function " + declaration->name() + " expects " +
                           std::to_string(declaration->params().size()) + " arguments");
      }

      auto call_environment = std::make_shared<Environment>(function.closure);
      for (std::size_t i = 0; i < declaration->params().size(); ++i) {
        call_environment->define(declaration->params()[i], arguments[i]);
      }

      try {
        executeBlock(declaration->body(), call_environment);
        return Value::undefined();
      } catch (const ReturnSignal& signal) {
        return signal.value();
      }
    }
    if (!callee.isFunction()) {
      if (callee_variable != nullptr) {
        throw RuntimeError(callee_variable->name() + " is not callable");
      }
      throw RuntimeError("value is not callable");
    }
  }

  if (const auto* call = dynamic_cast<const MethodCallExpr*>(&expression)) {
    Value object = evaluate(call->object());
    std::vector<Value> arguments;
    for (auto& argument : call->arguments()) {
      arguments.push_back(evaluate(*argument));
    }
    if (object.isArray()) {
      return callArrayMethod(object, call->name(), arguments);
    }

    if (object.isInstance()) {
      auto instance = object.asInstance();
      const FunctionValue* method = findMethod(instance->klass, call->name());
      if (method == nullptr) {
        throw RuntimeError("value has no method: " + call->name());
      }

      return callMethod(instance, *method, arguments);
    }

    throw RuntimeError("value has no method: " + call->name());
  }
  if (const auto* call = dynamic_cast<const SuperCallExpr*>(&expression)) {
    Value superValue;
    Value thisValue;
    try {
      superValue = environment_->get("super");
      thisValue = environment_->get("this");
    } catch (const RuntimeError&) {
      throw RuntimeError("super outside subclass method");
    }
    if (!superValue.isClass()) {
      throw RuntimeError("superclass must be a class");
    }
    if (!thisValue.isInstance()) {
      throw RuntimeError("this must be an instance");
    }

    std::vector<Value> arguments;
    for (const ExprPtr& argument : call->arguments()) {
      arguments.push_back(evaluate(*argument));
    }

    const FunctionValue* method = findMethod(superValue.asClass(), call->method());
    if (method == nullptr) {
      throw RuntimeError("superclass has no method: " + call->method());
    }

    return callMethod(thisValue.asInstance(), *method, arguments);
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

    if (object.isInstance()) {
      auto instance = object.asInstance();
      auto field = instance->fields.find(get->name());
      if (field != instance->fields.end()) {
        return field->second;
      }

      const FunctionValue* method = findMethod(instance->klass, get->name());
      if (method != nullptr) {
        auto bound_method = std::make_shared<BoundMethodValue>();
        bound_method->receiver = instance;
        bound_method->method = *method;
        return Value(bound_method);
      }
      return Value::undefined();
    }


    const auto& properties = object.asObject();
    auto it = properties.find(get->name());
    if (it == properties.end()) {
      return Value::undefined();
    }

    return it->second;
  }

  if (const auto* set = dynamic_cast<const SetExpr*>(&expression)) {
    Value object = evaluate(set->object());
    Value value = evaluate(set->value());

    if (object.isObject()) {
      object.asObject()[set->name()] = value;
      return value;
    }

    if (object.isInstance()) {
      object.asInstance()->fields[set->name()] = value;
      return value;
    }
  }
  throw RuntimeError("unknown expression type");
}

Value Interpreter::callMethod(std::shared_ptr<InstanceValue> receiver, const FunctionValue& method,
                              const std::vector<Value>& arguments) {
  const FunctionStmt* declaration = method.declaration;
  if (declaration == nullptr) {
    throw RuntimeError("value is not callable");
  }

  if (arguments.size() != declaration->params().size()) {
    throw RuntimeError("method " + declaration->name() + " expects " +
                       std::to_string(declaration->params().size()) + " arguments");
  }

  auto method_environment = std::make_shared<Environment>(method.closure);
  method_environment->define("this", Value(std::move(receiver)));
  for (std::size_t i = 0; i < declaration->params().size(); ++i) {
    method_environment->define(declaration->params()[i], arguments[i]);
  }

  try {
    executeBlock(declaration->body(), method_environment);
    return Value::undefined();
  } catch (const ReturnSignal& signal) {
    return signal.value();
  }
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
