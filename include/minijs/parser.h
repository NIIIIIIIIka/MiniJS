#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "minijs/ast.h"
#include "minijs/diagnostic.h"
#include "minijs/token.h"

namespace minijs {

// 递归下降语法分析器，将源码解析为表达式或完整程序 AST。
class Parser {
 public:
  explicit Parser(std::string_view source);

  // 解析单个表达式，主要用于表达式级别测试。
  ExprPtr parse();

  // 解析完整脚本，返回顶层语句列表。
  Program parseProgram();

  // 返回词法和语法分析阶段收集到的诊断信息。
  const std::vector<Diagnostic>& diagnostics() const;

 private:
  // 解析一条语句，并根据当前 token 分派到具体语句解析函数。
  StmtPtr statement();

  // 解析 let 变量声明。
  StmtPtr letDeclaration();

  // 解析表达式语句。
  StmtPtr expressionStatement();

  // 解析 if/else 条件语句。
  StmtPtr ifStatement();

  // 解析花括号包裹的块语句。
  StmtPtr blockStatement();

  // 解析 while 循环语句。
  StmtPtr whileStatement();

  StmtPtr forStatement();

  // 解析 return 返回语句。
  StmtPtr returnStatement();

  // 解析函数声明。
  StmtPtr functionDeclaration();

  StmtPtr breakStatement();

  StmtPtr continueStatement();

  // 解析块内部的语句列表，调用方负责先消费左花括号。
  Program block();

  // 解析表达式入口，目前最低优先级是赋值表达式。
  ExprPtr expression();

  // 解析右结合赋值表达式。
  ExprPtr assignment();

  // 解析相等比较表达式。
  ExprPtr equality();

  ExprPtr logicalOr();

  ExprPtr logicalAnd();

  // 解析大小比较表达式。
  ExprPtr comparison();

  // 解析加减表达式。
  ExprPtr term();

  // 解析乘除取模表达式。
  ExprPtr factor();

  // 一元运算
  ExprPtr unary();

  // 解析函数调用表达式。
  ExprPtr call();

  // 解析基础表达式：数字、变量和括号表达式。
  ExprPtr primary();

  // 如果当前 token 类型匹配，则消费它并返回 true。
  bool match(TokenType type);

  // 检查当前 token 类型但不消费。
  bool check(TokenType type) const;

  // 返回是否已经到达 EOF token。
  bool isAtEnd() const;

  // 消费当前 token 并返回刚消费的 token。
  const Token& advance();

  // 返回当前 token。
  const Token& peek() const;

  // 返回上一个已消费 token。
  const Token& previous() const;

  // 记录一条语法诊断信息。
  void report(const Token& token, std::string message);

  std::vector<Token> tokens_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t current_ = 0;
};

}  // namespace minijs
