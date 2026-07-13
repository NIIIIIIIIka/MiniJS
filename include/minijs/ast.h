#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "minijs/token.h"

namespace minijs {

// 所有表达式 AST 节点的基类。
class Expr {
 public:
  virtual ~Expr() = default;
};

// 表达式节点的独占所有权指针。
using ExprPtr = std::unique_ptr<Expr>;

// 所有语句 AST 节点的基类。
class Stmt {
 public:
  virtual ~Stmt() = default;
};

// 语句节点的独占所有权指针。
using StmtPtr = std::unique_ptr<Stmt>;

// 一段程序，或块/函数体中的语句列表。
using Program = std::vector<StmtPtr>;

// 对象字面量中的单个属性，保存属性名和还没有执行的值表达式。
struct ObjectProperty {
  std::string name;
  ExprPtr value;
};

// 数字字面量表达式，保留源码中的原始文本。
class NumberExpr final : public Expr {
 public:
  explicit NumberExpr(std::string value);
  const std::string& value() const;

 private:
  std::string value_;
};

// 字符串字面量表达式，保存去掉引号后的字符串内容。
class StringExpr final : public Expr {
 public:
  explicit StringExpr(std::string value);
  const std::string& value() const;

 private:
  std::string value_;
};

// 布尔字面量表达式。
class BoolExpr final : public Expr {
 public:
  explicit BoolExpr(std::string value);
  const std::string& value() const;

 private:
  std::string value_;
};

// null 字面量表达式。
class NullExpr final : public Expr {
 public:
  explicit NullExpr(std::string value);
  const std::string& value() const;

 private:
  std::string value_;
};

// undefined 字面量表达式。
class UndefinedExpr final : public Expr {
 public:
  UndefinedExpr() = default;
};

// 变量读取表达式。
class VariableExpr final : public Expr {
 public:
  explicit VariableExpr(std::string name);
  const std::string& name() const;

 private:
  std::string name_;
};

// 数组字面量表达式，例如 [1, x + 2]。
class ArrayExpr final : public Expr {
 public:
  explicit ArrayExpr(std::vector<ExprPtr> elements);
  const std::vector<ExprPtr>& elements() const;

 private:
  std::vector<ExprPtr> elements_;
};

// 对象字面量表达式，例如 { age: 18 }。
class ObjectExpr final : public Expr {
 public:
  explicit ObjectExpr(std::vector<ObjectProperty> properties);
  const std::vector<ObjectProperty>& properties() const;

 private:
  std::vector<ObjectProperty> properties_;
};

class UnaryExpr final : public Expr {
 public:
  UnaryExpr(TokenType op, ExprPtr right);

  TokenType op() const;
  const Expr& right() const;

 private:
  TokenType op_;
  ExprPtr right_;
};

// 二元运算表达式，例如 a + b 或 x <= y。
class BinaryExpr final : public Expr {
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

class LogicalExpr final : public Expr {
 public:
  LogicalExpr(ExprPtr left, TokenType op, ExprPtr right);

  const Expr& left() const;
  TokenType op() const;
  const Expr& right() const;

 private:
  ExprPtr left_;
  TokenType op_;
  ExprPtr right_;
};

// 括号表达式。
class GroupingExpr final : public Expr {
 public:
  explicit GroupingExpr(ExprPtr expression);
  const Expr& expression() const;

 private:
  ExprPtr expression_;
};

// 变量赋值表达式，例如 x = value。
class AssignExpr final : public Expr {
 public:
  AssignExpr(std::string name, ExprPtr value);
  const std::string& name() const;
  const Expr& value() const;

 private:
  std::string name_;
  ExprPtr value_;
};

// 数组下标读取表达式，例如 array[index]。
class IndexExpr final : public Expr {
 public:
  IndexExpr(ExprPtr object, ExprPtr index);
  const Expr& object() const;
  const Expr& index() const;
  ExprPtr takeObject();
  ExprPtr takeIndex();

 private:
  ExprPtr object_;
  ExprPtr index_;
};

// 数组下标赋值表达式，例如 array[index] = value。
class IndexAssignExpr final : public Expr {
 public:
  IndexAssignExpr(ExprPtr object, ExprPtr index, ExprPtr value);
  const Expr& object() const;
  const Expr& index() const;
  const Expr& value() const;

 private:
  ExprPtr object_;
  ExprPtr index_;
  ExprPtr value_;
};

// 对象属性读取表达式，例如 object.name。
class GetExpr final : public Expr {
 public:
  GetExpr(ExprPtr object, std::string name);
  const Expr& object() const;
  const std::string& name() const;
  ExprPtr takeObject();

 private:
  ExprPtr object_;
  std::string name_;
};

// 对象属性赋值表达式，例如 object.name = value。
class SetExpr final : public Expr {
 public:
  SetExpr(ExprPtr object, std::string name, ExprPtr value);
  const Expr& object() const;
  const std::string& name() const;
  const Expr& value() const;

 private:
  ExprPtr object_;
  std::string name_;
  ExprPtr value_;
};

class SuperCallExpr final : public Expr {
 public:
  SuperCallExpr(std::string method, std::vector<ExprPtr> arguments);

  const std::string& method() const;
  const std::vector<ExprPtr>& arguments() const;

 private:
  std::string method_;
  std::vector<ExprPtr> arguments_;
};

// 具名函数调用表达式。
class CallExpr final : public Expr {
 public:
  CallExpr(ExprPtr callee, std::vector<ExprPtr> arguments);

  const Expr& callee() const;
  const std::vector<ExprPtr>& arguments() const;

 private:
  ExprPtr callee_;
  std::vector<ExprPtr> arguments_;
};

// 内置方法调用表达式，例如 array.push(value)。
class MethodCallExpr final : public Expr {
 public:
  MethodCallExpr(ExprPtr object, std::string name, std::vector<ExprPtr> arguments);
  const Expr& object() const;
  const std::string& name() const;
  const std::vector<ExprPtr>& arguments() const;

 private:
  ExprPtr object_;
  std::string name_;
  std::vector<ExprPtr> arguments_;
};

// 表达式语句，用于执行表达式副作用或保存最后结果。
class ExprStmt final : public Stmt {
 public:
  explicit ExprStmt(ExprPtr expression);
  const Expr& expression() const;

 private:
  ExprPtr expression_;
};

// let 变量声明语句。
class LetStmt final : public Stmt {
 public:
  LetStmt(std::string name, ExprPtr initializer);
  const std::string& name() const;
  const Expr& initializer() const;

 private:
  std::string name_;
  ExprPtr initializer_;
};

// if/else 条件语句，else 分支可以为空。
class IfStmt final : public Stmt {
 public:
  IfStmt(ExprPtr condition, StmtPtr thenBranch, StmtPtr elseBranch);
  const Expr& condition() const;
  const Stmt& thenBranch() const;
  const Stmt* elseBranch() const;

 private:
  ExprPtr condition_;
  StmtPtr thenBranch_;
  StmtPtr elseBranch_;
};

// 花括号包裹的块语句。
class BlockStmt final : public Stmt {
 public:
  explicit BlockStmt(Program statements);
  const Program& statements() const;

 private:
  Program statements_;
};

// while 循环语句。
class WhileStmt final : public Stmt {
 public:
  WhileStmt(ExprPtr condition, StmtPtr body);
  const Expr& condition() const;
  const Stmt& body() const;

 private:
  ExprPtr condition_;
  StmtPtr body_;
};

class BreakStmt final : public Stmt {
 public:
  BreakStmt() = default;
};
class ContinueStmt final : public Stmt {
 public:
  ContinueStmt() = default;
};

// 具名函数声明语句。
class FunctionStmt final : public Stmt {
 public:
  FunctionStmt(std::string name, std::vector<std::string> params, Program body);
  const std::string& name() const;
  const std::vector<std::string>& params() const;
  const Program& body() const;

 private:
  std::string name_;
  std::vector<std::string> params_;
  Program body_;
};

struct ClassMethod {
  std::shared_ptr<FunctionStmt> function;
  bool isStatic = false;
};

class ForStmt final : public Stmt {
 public:
  ForStmt(StmtPtr initializer, ExprPtr condition, ExprPtr increment, StmtPtr body);

  const Stmt* initializer() const;
  const Expr* condition() const;
  const Expr* increment() const;
  const Stmt& body() const;

 private:
  StmtPtr initializer_;
  ExprPtr condition_;
  ExprPtr increment_;
  StmtPtr body_;
};

// return 返回语句。
class ReturnStmt final : public Stmt {
 public:
  explicit ReturnStmt(ExprPtr value);
  const Expr* value() const;

 private:
  ExprPtr value_;
};

// 类声明语句。实例字段通过 this.x 动态创建；superclass 为空表示没有父类。
class ClassStmt final : public Stmt {
 public:
  ClassStmt(std::string name, std::optional<std::string> superclass,
            std::vector<ClassMethod> methods);

  const std::string& name() const;
  const std::optional<std::string>& superclass() const;
  const std::vector<ClassMethod>& methods() const;

 private:
  std::string name_;
  std::optional<std::string> superclass_;
  std::vector<ClassMethod> methods_;
};


std::string formatExpr(const Expr& expression);
std::string formatStmt(const Stmt& statement);
std::string formatProgram(const Program& program);

}  // namespace minijs
