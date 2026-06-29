#pragma once

#include <memory>
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

// 数字字面量表达式，保留源码中的原始文本。
class NumberExpr final : public Expr {
 public:
  explicit NumberExpr(std::string value);

  // 返回数字字面量文本，解释执行阶段再转换为数值。
  const std::string& value() const;

 private:
  std::string value_;
};

// 布尔字面量表达式。
class BoolExpr final : public Expr {
 public:
  explicit BoolExpr(std::string value);

  // 返回布尔字面量文本。
  const std::string& value() const;

 private:
  std::string value_;
};

// null 字面量表达式。
class NullExpr final : public Expr {
 public:
  explicit NullExpr(std::string value);

  // 返回 null 字面量文本。
  const std::string& value() const;

 private:
  std::string value_;
};

// 变量读取表达式。
class VariableExpr final : public Expr {
 public:
  explicit VariableExpr(std::string name);

  // 返回变量名。
  const std::string& name() const;

 private:
  std::string name_;
};

// 数组字面量表达式，例如 [1, x + 2]。
class ArrayExpr final : public Expr {
 public:
  explicit ArrayExpr(std::vector<ExprPtr> elements);

  // 返回数组元素表达式列表。
  const std::vector<ExprPtr>& elements() const;

 private:
  std::vector<ExprPtr> elements_;
};

// 二元运算表达式，例如 a + b 或 x <= y。
class BinaryExpr final : public Expr {
 public:
  BinaryExpr(ExprPtr left, TokenType op, ExprPtr right);

  // 返回左操作数。
  const Expr& left() const;

  // 返回运算符 token 类型。
  TokenType op() const;

  // 返回右操作数。
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

  // 返回括号内部的表达式。
  const Expr& expression() const;

 private:
  ExprPtr expression_;
};

// 变量赋值表达式，例如 x = value。
class AssignExpr final : public Expr {
 public:
  AssignExpr(std::string name, ExprPtr value);

  // 返回被赋值的变量名。
  const std::string& name() const;

  // 返回产生新值的表达式。
  const Expr& value() const;

 private:
  std::string name_;
  ExprPtr value_;
};

// 数组下标读取表达式，例如 array[index]。
class IndexExpr final : public Expr {
 public:
  IndexExpr(ExprPtr object, ExprPtr index);

  // 返回被索引的对象表达式。
  const Expr& object() const;

  // 返回下标表达式。
  const Expr& index() const;

  // 转移被索引对象的所有权，用于构造下标赋值表达式。
  ExprPtr takeObject();

  // 转移下标表达式的所有权，用于构造下标赋值表达式。
  ExprPtr takeIndex();

 private:
  ExprPtr object_;
  ExprPtr index_;
};

// 数组下标赋值表达式，例如 array[index] = value。
class IndexAssignExpr final : public Expr {
 public:
  IndexAssignExpr(ExprPtr object, ExprPtr index, ExprPtr value);

  // 返回被索引的对象表达式。
  const Expr& object() const;

  // 返回下标表达式。
  const Expr& index() const;

  // 返回赋给数组元素的新值表达式。
  const Expr& value() const;

 private:
  ExprPtr object_;
  ExprPtr index_;
  ExprPtr value_;
};

// 具名函数调用表达式。
class CallExpr final : public Expr {
 public:
  CallExpr(std::string callee, std::vector<ExprPtr> arguments);

  // 返回被调用的函数名。
  const std::string& callee() const;

  // 返回按源码顺序保存的实参表达式。
  const std::vector<ExprPtr>& arguments() const;

 private:
  std::string callee_;
  std::vector<ExprPtr> arguments_;
};

// 表达式语句，用于执行表达式副作用或保存最后结果。
class ExprStmt final : public Stmt {
 public:
  explicit ExprStmt(ExprPtr expression);

  // 返回被包装的表达式。
  const Expr& expression() const;

 private:
  ExprPtr expression_;
};

// let 变量声明语句。
class LetStmt final : public Stmt {
 public:
  LetStmt(std::string name, ExprPtr initializer);

  // 返回声明的变量名。
  const std::string& name() const;

  // 返回初始化表达式。
  const Expr& initializer() const;

 private:
  std::string name_;
  ExprPtr initializer_;
};

// if/else 条件语句，else 分支可以为空。
class IfStmt final : public Stmt {
 public:
  IfStmt(ExprPtr condition, StmtPtr thenBranch, StmtPtr elseBranch);

  // 返回条件表达式。
  const Expr& condition() const;

  // 返回条件为真时执行的语句。
  const Stmt& thenBranch() const;

  // 返回可选 else 分支；没有 else 时返回 nullptr。
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

  // 返回块内部的语句列表。
  const Program& statements() const;

 private:
  Program statements_;
};

// while 循环语句。
class WhileStmt final : public Stmt {
 public:
  WhileStmt(ExprPtr condition, StmtPtr body);

  // 返回循环条件表达式。
  const Expr& condition() const;

  // 返回每轮循环执行的语句。
  const Stmt& body() const;

 private:
  ExprPtr condition_;
  StmtPtr body_;
};

// 具名函数声明语句。
class FunctionStmt final : public Stmt {
 public:
  FunctionStmt(std::string name, std::vector<std::string> params, Program body);

  // 返回函数名。
  const std::string& name() const;

  // 返回形参名列表。
  const std::vector<std::string>& params() const;

  // 返回函数体语句列表。
  const Program& body() const;

 private:
  std::string name_;
  std::vector<std::string> params_;
  Program body_;
};

// return 返回语句。
class ReturnStmt final : public Stmt {
 public:
  explicit ReturnStmt(ExprPtr value);

  // 返回 return 后面的表达式。
  const Expr& value() const;

 private:
  ExprPtr value_;
};

// 将表达式格式化为紧凑的前缀形式，方便调试和测试。
std::string formatExpr(const Expr& expression);

// 将语句格式化为紧凑的前缀形式，方便调试和测试。
std::string formatStmt(const Stmt& statement);

// 将程序格式化为按行分隔的调试文本。
std::string formatProgram(const Program& program);

}  // namespace minijs
