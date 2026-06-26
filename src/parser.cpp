#include "minijs/parser.h"

#include <string>
#include <utility>

#include "minijs/lexer.h"

namespace minijs {

Parser::Parser(std::string_view source) {
  Lexer lexer(source);

  while (true) {
    Token token = lexer.nextToken();
    tokens_.push_back(token);

    if (token.type == TokenType::Eof) {
      break;
    }
  }

  diagnostics_ = lexer.diagnostics();
}

ExprPtr Parser::parse() {
  ExprPtr result = expression();

  if (match(TokenType::Semicolon)) {
    return result;
  }

  if (!isAtEnd()) {
    report(peek(), "expected end of expression");
  }

  return result;
}

Program Parser::parseProgram() {
  Program statements;

  while (!isAtEnd()) {
    statements.push_back(statement());
  }

  return statements;
}

const std::vector<Diagnostic>& Parser::diagnostics() const { return diagnostics_; }

StmtPtr Parser::statement() {
  if (match(TokenType::If)) {
    return ifStatement();
  }
  if (match(TokenType::Let)) {
    return letDeclaration();
  }

  return expressionStatement();
}

StmtPtr Parser::letDeclaration() {
  if (!check(TokenType::Identifier)) {
    report(peek(), "expected variable name");
    return nullptr;
  }

  const Token name = advance();

  if (!match(TokenType::Equal)) {
    report(peek(), "expected '=' after variable name");
  }

  ExprPtr initializer = expression();

  if (!match(TokenType::Semicolon)) {
    report(peek(), "expected ';' after variable declaration");
  }

  return std::make_unique<LetStmt>(std::string(name.lexeme), std::move(initializer));
}

StmtPtr Parser::expressionStatement() {
  ExprPtr expr = expression();

  if (!match(TokenType::Semicolon)) {
    report(peek(), "expected ';' after expression");
  }

  return std::make_unique<ExprStmt>(std::move(expr));
}

StmtPtr Parser::ifStatement() {
  if (!match(TokenType::LeftParen)) {
    report(peek(), "expected '(' after if");
  }
  ExprPtr condition = expression();
  if (!match(TokenType::RightParen)) {
    report(peek(), "expected ')' after if condition");
  }
  StmtPtr thenBranch = statement();
  StmtPtr elseBranch;

  if (match(TokenType::Else)) {
    elseBranch = statement();
  }

  return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch),
                                  std::move(elseBranch));
}

ExprPtr Parser::expression() { return equality(); }

ExprPtr Parser::equality() {
  ExprPtr expr = comparison();
  while (match(TokenType::EqualEqual) || match(TokenType::BangEqual)) {
    const TokenType op = previous().type;
    ExprPtr right = comparison();
    expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
  }
  return expr;
}

ExprPtr Parser::comparison() {
  ExprPtr expr = term();
  while (match(TokenType::Greater) || match(TokenType::GreaterEqual) || match(TokenType::Less) ||
         match(TokenType::LessEqual)) {
    const TokenType op = previous().type;
    ExprPtr right = term();
    expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
  }
  return expr;
}

ExprPtr Parser::term() {
  ExprPtr expr = factor();

  while (match(TokenType::Plus) || match(TokenType::Minus)) {
    const TokenType op = previous().type;
    ExprPtr right = factor();
    expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
  }
  return expr;
}

ExprPtr Parser::factor() {
  ExprPtr expr = primary();

  while (match(TokenType::Star) || match(TokenType::Slash) || match(TokenType::Percent)) {
    const TokenType op = previous().type;
    ExprPtr right = primary();
    expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
  }

  return expr;
}

ExprPtr Parser::primary() {
  if (match(TokenType::Number)) {
    const Token& token = previous();
    return std::make_unique<NumberExpr>(std::string(token.lexeme));
  }

  if (match(TokenType::Identifier)) {
    const Token& token = previous();
    return std::make_unique<VariableExpr>(std::string(token.lexeme));
  }

  if (match(TokenType::LeftParen)) {
    ExprPtr expr = expression();

    if (!match(TokenType::RightParen)) {
      report(peek(), "expected ')' after expression");
    }

    return std::make_unique<GroupingExpr>(std::move(expr));
  }

  report(peek(), "expected expression");
  if (!isAtEnd()) {
    advance();
  }
  return std::make_unique<NumberExpr>("0");
}

bool Parser::match(TokenType type) {
  if (!check(type)) {
    return false;
  }

  advance();
  return true;
}

bool Parser::check(TokenType type) const {
  if (isAtEnd()) {
    return type == TokenType::Eof;
  }

  return peek().type == type;
}

bool Parser::isAtEnd() const { return peek().type == TokenType::Eof; }

const Token& Parser::advance() {
  if (!isAtEnd()) {
    ++current_;
  }

  return previous();
}

const Token& Parser::peek() const { return tokens_[current_]; }

const Token& Parser::previous() const { return tokens_[current_ - 1]; }

void Parser::report(const Token& token, std::string message) {
  diagnostics_.emplace_back(token.location, std::move(message));
}

}  // namespace minijs
