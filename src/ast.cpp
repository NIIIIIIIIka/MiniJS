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

BoolExpr::BoolExpr(std::string value) : value_(std::move(value)) {}

const std::string& BoolExpr::value() const { return value_; }

NullExpr::NullExpr(std::string value) : value_(std::move(value)) {}

const std::string& NullExpr::value() const { return value_; }

VariableExpr::VariableExpr(std::string name) : name_(std::move(name)) {}

const std::string& VariableExpr::name() const { return name_; }

ArrayExpr::ArrayExpr(std::vector<ExprPtr> elements) : elements_(std::move(elements)) {}

const std::vector<ExprPtr>& ArrayExpr::elements() const { return elements_; }

ObjectExpr::ObjectExpr(std::vector<ObjectProperty> properties)
    : properties_(std::move(properties)) {}

const std::vector<ObjectProperty>& ObjectExpr::properties() const { return properties_; }

BinaryExpr::BinaryExpr(ExprPtr left, TokenType op, ExprPtr right)
    : left_(std::move(left)), op_(op), right_(std::move(right)) {}

const Expr& BinaryExpr::left() const { return *left_; }

TokenType BinaryExpr::op() const { return op_; }

const Expr& BinaryExpr::right() const { return *right_; }

GroupingExpr::GroupingExpr(ExprPtr expression) : expression_(std::move(expression)) {}

const Expr& GroupingExpr::expression() const { return *expression_; }

AssignExpr::AssignExpr(std::string name, ExprPtr value)
    : name_(std::move(name)), value_(std::move(value)) {}

const std::string& AssignExpr::name() const { return name_; }

const Expr& AssignExpr::value() const { return *value_; }

IndexExpr::IndexExpr(ExprPtr object, ExprPtr index)
    : object_(std::move(object)), index_(std::move(index)) {}

const Expr& IndexExpr::object() const { return *object_; }

const Expr& IndexExpr::index() const { return *index_; }

ExprPtr IndexExpr::takeObject() { return std::move(object_); }

ExprPtr IndexExpr::takeIndex() { return std::move(index_); }

IndexAssignExpr::IndexAssignExpr(ExprPtr object, ExprPtr index, ExprPtr value)
    : object_(std::move(object)), index_(std::move(index)), value_(std::move(value)) {}

const Expr& IndexAssignExpr::object() const { return *object_; }

const Expr& IndexAssignExpr::index() const { return *index_; }

const Expr& IndexAssignExpr::value() const { return *value_; }

GetExpr::GetExpr(ExprPtr object, std::string name)
    : object_(std::move(object)), name_(std::move(name)) {}

const Expr& GetExpr::object() const { return *object_; }

const std::string& GetExpr::name() const { return name_; }

ExprPtr GetExpr::takeObject() { return std::move(object_); }

SetExpr::SetExpr(ExprPtr object, std::string name, ExprPtr value)
    : object_(std::move(object)), name_(std::move(name)), value_(std::move(value)) {}

const Expr& SetExpr::object() const { return *object_; }

const std::string& SetExpr::name() const { return name_; }

const Expr& SetExpr::value() const { return *value_; }

CallExpr::CallExpr(std::string callee, std::vector<ExprPtr> arguments)
    : callee_(std::move(callee)), arguments_(std::move(arguments)) {}

const std::string& CallExpr::callee() const { return callee_; }

const std::vector<ExprPtr>& CallExpr::arguments() const { return arguments_; }

ExprStmt::ExprStmt(ExprPtr expression) : expression_(std::move(expression)) {}

const Expr& ExprStmt::expression() const { return *expression_; }

LetStmt::LetStmt(std::string name, ExprPtr initializer)
    : name_(std::move(name)), initializer_(std::move(initializer)) {}

const std::string& LetStmt::name() const { return name_; }

const Expr& LetStmt::initializer() const { return *initializer_; }

IfStmt::IfStmt(ExprPtr condition, StmtPtr thenBranch, StmtPtr elseBranch)
    : condition_(std::move(condition)),
      thenBranch_(std::move(thenBranch)),
      elseBranch_(std::move(elseBranch)) {}

const Expr& IfStmt::condition() const { return *condition_; }

const Stmt& IfStmt::thenBranch() const { return *thenBranch_; }

const Stmt* IfStmt::elseBranch() const { return elseBranch_.get(); }

BlockStmt::BlockStmt(Program statements) : statements_(std::move(statements)) {}

const Program& BlockStmt::statements() const { return statements_; }

WhileStmt::WhileStmt(ExprPtr condition, StmtPtr body)
    : condition_(std::move(condition)), body_(std::move(body)) {}

const Expr& WhileStmt::condition() const { return *condition_; }

const Stmt& WhileStmt::body() const { return *body_; }

FunctionStmt::FunctionStmt(std::string name, std::vector<std::string> params, Program body)
    : name_(std::move(name)), params_(std::move(params)), body_(std::move(body)) {}

const std::string& FunctionStmt::name() const { return name_; }

const std::vector<std::string>& FunctionStmt::params() const { return params_; }

const Program& FunctionStmt::body() const { return body_; }

ReturnStmt::ReturnStmt(ExprPtr value) : value_(std::move(value)) {}

const Expr& ReturnStmt::value() const { return *value_; }

std::string formatExpr(const Expr& expression) {
  if (const auto* number = dynamic_cast<const NumberExpr*>(&expression)) {
    return number->value();
  }

  if (const auto* boolean = dynamic_cast<const BoolExpr*>(&expression)) {
    return boolean->value();
  }

  if (const auto* null = dynamic_cast<const NullExpr*>(&expression)) {
    return null->value();
  }

  if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
    return variable->name();
  }

  if (const auto* array = dynamic_cast<const ArrayExpr*>(&expression)) {
    std::string result = "(array";
    for (const ExprPtr& element : array->elements()) {
      result += " ";
      result += formatExpr(*element);
    }
    result += ")";
    return result;
  }

  if (const auto* object = dynamic_cast<const ObjectExpr*>(&expression)) {
    std::string result = "(object";
    for (const ObjectProperty& property : object->properties()) {
      result += " (";
      result += property.name;
      result += " ";
      result += formatExpr(*property.value);
      result += ")";
    }
    result += ")";
    return result;
  }

  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
    return "(" + binaryOpName(binary->op()) + " " + formatExpr(binary->left()) + " " +
           formatExpr(binary->right()) + ")";
  }

  if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression)) {
    return "(group " + formatExpr(grouping->expression()) + ")";
  }

  if (const auto* assign = dynamic_cast<const AssignExpr*>(&expression)) {
    return "(assign " + assign->name() + " " + formatExpr(assign->value()) + ")";
  }

  if (const auto* index = dynamic_cast<const IndexExpr*>(&expression)) {
    return "(index " + formatExpr(index->object()) + " " + formatExpr(index->index()) + ")";
  }

  if (const auto* indexAssign = dynamic_cast<const IndexAssignExpr*>(&expression)) {
    return "(index-assign " + formatExpr(indexAssign->object()) + " " +
           formatExpr(indexAssign->index()) + " " + formatExpr(indexAssign->value()) + ")";
  }

  if (const auto* get = dynamic_cast<const GetExpr*>(&expression)) {
    return "(get " + formatExpr(get->object()) + " " + get->name() + ")";
  }

  if (const auto* set = dynamic_cast<const SetExpr*>(&expression)) {
    return "(set " + formatExpr(set->object()) + " " + set->name() + " " +
           formatExpr(set->value()) + ")";
  }

  if (const auto* call = dynamic_cast<const CallExpr*>(&expression)) {
    std::string result = "(call " + call->callee();
    for (const ExprPtr& arg : call->arguments()) {
      result += " " + formatExpr(*arg);
    }
    result += ")";
    return result;
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

  if (const auto* blockStmt = dynamic_cast<const BlockStmt*>(&statement)) {
    std::string result = "(block";
    for (const StmtPtr& inner : blockStmt->statements()) {
      if (inner) {
        result += " ";
        result += formatStmt(*inner);
      }
    }
    result += ")";
    return result;
  }

  if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&statement)) {
    return "(while " + formatExpr(whileStmt->condition()) + " " + formatStmt(whileStmt->body()) +
           ")";
  }

  if (const auto* functionStmt = dynamic_cast<const FunctionStmt*>(&statement)) {
    std::string result = "(function " + functionStmt->name() + " (";
    for (std::size_t i = 0; i < functionStmt->params().size(); ++i) {
      if (i != 0) {
        result += " ";
      }
      result += functionStmt->params()[i];
    }
    result += ") (block";
    for (const StmtPtr& inner : functionStmt->body()) {
      if (inner) {
        result += " ";
        result += formatStmt(*inner);
      }
    }
    result += "))";
    return result;
  }

  if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&statement)) {
    return "(return " + formatExpr(returnStmt->value()) + ")";
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

}  // namespace minijs
