#pragma once

#include <memory>
#include <string>
#include <vector>

#include "minijs/token.h"

namespace minijs {

class Expr {
 public:
  virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

class Stmt {
 public:
  virtual ~Stmt() = default;
};

using StmtPtr = std::unique_ptr<Stmt>;
using Program = std::vector<StmtPtr>;

class NumberExpr final : public Expr {
 public:
  explicit NumberExpr(std::string value);

  const std::string& value() const;

 private:
  std::string value_;
};

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

class GroupingExpr final : public Expr {
 public:
  explicit GroupingExpr(ExprPtr expression);

  const Expr& expression() const;

 private:
  ExprPtr expression_;
};

class VariableExpr final : public Expr {
 public:
  explicit VariableExpr(std::string name);

  const std::string& name() const;

 private:
  std::string name_;
};

class ExprStmt final : public Stmt {
 public:
  explicit ExprStmt(ExprPtr expression);

  const Expr& expression() const;

 private:
  ExprPtr expression_;
};

class LetStmt final : public Stmt {
 public:
  LetStmt(std::string name, ExprPtr initializer);

  const std::string& name() const;
  const Expr& initializer() const;

 private:
  std::string name_;
  ExprPtr initializer_;
};

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
class BlockStmt final : public Stmt {
 public:
  explicit BlockStmt(Program statements);

  const Program& statements() const;

 private:
  Program statements_;
};

class AssignExpr final : public Expr {
 public:
  AssignExpr(std::string name, ExprPtr value);

  const std::string& name() const;
  const Expr& value() const;

 private:
  std::string name_;
  ExprPtr value_;
};

class WhileStmt final : public Stmt {
 public:
  WhileStmt(ExprPtr condition, StmtPtr body);

  const Expr& condition() const;
  const Stmt& body() const;

 private:
  ExprPtr condition_;
  StmtPtr body_;
};
std::string formatExpr(const Expr& expression);
std::string formatStmt(const Stmt& statement);
std::string formatProgram(const Program& program);

}  // namespace minijs
