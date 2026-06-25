#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "minijs/ast.h"
#include "minijs/diagnostic.h"
#include "minijs/token.h"

namespace minijs {

class Parser {
 public:
  explicit Parser(std::string_view source);

  ExprPtr parse();
  Program parseProgram();

  const std::vector<Diagnostic>& diagnostics() const;

 private:
  StmtPtr statement();
  StmtPtr letDeclaration();
  StmtPtr expressionStatement();

  ExprPtr expression();
  ExprPtr term();
  ExprPtr factor();
  ExprPtr primary();

  bool match(TokenType type);
  bool check(TokenType type) const;
  bool isAtEnd() const;
  const Token& advance();
  const Token& peek() const;
  const Token& previous() const;

  void report(const Token& token, std::string message);

  std::vector<Token> tokens_;
  std::vector<Diagnostic> diagnostics_;
  std::size_t current_ = 0;
};

}  // namespace minijs
