#include "minijs/parser.h"

#include <string>
#include <utility>

#include "minijs/lexer.h"

namespace minijs {
namespace {
std::string decodeStringLiteral(std::string_view lexeme) {
  std::string result;

  if (lexeme.size() < 2) {
    return result;
  }

  for (std::size_t i = 1; i + 1 < lexeme.size(); ++i) {
    if (lexeme[i] == '\\' && i + 1 < lexeme.size() - 1) {
      ++i;
      switch (lexeme[i]) {
        case '"':
          result += '"';
          break;
        case '\\':
          result += '\\';
          break;
        case 'n':
          result += '\n';
          break;
        case 't':
          result += '\t';
          break;
        default:
          result += lexeme[i];
          break;
      }
      continue;
    }

    result += lexeme[i];
  }

  return result;
}
}  // namespace

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
  if (match(TokenType::LeftBrace)) {
    return blockStatement();
  }
  if (match(TokenType::While)) {
    return whileStatement();
  }
  if (match(TokenType::Function)) {
    return functionDeclaration();
  }
  if (match(TokenType::Return)) {
    return returnStatement();
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

StmtPtr Parser::blockStatement() { return std::make_unique<BlockStmt>(block()); }

StmtPtr Parser::whileStatement() {
  if (!match(TokenType::LeftParen)) {
    report(peek(), "expected '(' after while");
  }
  ExprPtr condition = expression();
  if (!match(TokenType::RightParen)) {
    report(peek(), "expected ')' after while condition");
  }
  StmtPtr body = statement();

  return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

StmtPtr Parser::returnStatement() {
  ExprPtr value = expression();

  if (!match(TokenType::Semicolon)) {
    report(peek(), "expected ';' after return value");
  }

  return std::make_unique<ReturnStmt>(std::move(value));
}

StmtPtr Parser::functionDeclaration() {
  if (!check(TokenType::Identifier)) {
    report(peek(), "expected function name");
    return nullptr;
  }
  const Token name = advance();
  if (!match(TokenType::LeftParen)) {
    report(peek(), "expected '(' after function name");
  }
  std::vector<std::string> params;
  if (!check(TokenType::RightParen)) {
    do {
      if (!check(TokenType::Identifier)) {
        report(peek(), "expected parameter name");
        break;
      }

      params.push_back(std::string(advance().lexeme));
    } while (match(TokenType::Comma));
  }

  if (!match(TokenType::RightParen)) {
    report(peek(), "expected ')' after parameters");
  }

  if (!match(TokenType::LeftBrace)) {
    report(peek(), "expected '{' before function body");
    return nullptr;
  }

  Program body = block();
  return std::make_unique<FunctionStmt>(std::string(name.lexeme), std::move(params),
                                        std::move(body));
}

Program Parser::block() {
  Program statements;
  while (!check(TokenType::RightBrace) && !isAtEnd()) {
    statements.push_back(statement());
  }
  if (!match(TokenType::RightBrace)) {
    report(peek(), "expected '}' after block");
  }
  return statements;
}

ExprPtr Parser::expression() { return assignment(); }

ExprPtr Parser::assignment() {
  ExprPtr expr = equality();
  if (match(TokenType::Equal)) {
    ExprPtr value = assignment();
    if (const auto* variable = dynamic_cast<const VariableExpr*>(expr.get())) {
      return std::make_unique<AssignExpr>(variable->name(), std::move(value));
    }
    if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
      return std::make_unique<IndexAssignExpr>(index->takeObject(), index->takeIndex(),
                                               std::move(value));
    }
    if (auto* get = dynamic_cast<GetExpr*>(expr.get())) {
      return std::make_unique<SetExpr>(get->takeObject(), get->name(), std::move(value));
    }
    report(previous(), "invalid assignment target");
  }
  return expr;
}
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
  ExprPtr expr = call();

  while (match(TokenType::Star) || match(TokenType::Slash) || match(TokenType::Percent)) {
    const TokenType op = previous().type;
    ExprPtr right = call();
    expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right));
  }

  return expr;
}

ExprPtr Parser::call() {
  ExprPtr expr = primary();

  while (true) {
    //º¯Êýµ÷ÓÃ
    if (match(TokenType::LeftParen)) {
      std::vector<ExprPtr> arguments;

      if (!check(TokenType::RightParen)) {
        do {
          arguments.push_back(expression());
        } while (match(TokenType::Comma));
      }

      if (!match(TokenType::RightParen)) {
        report(peek(), "expected ')' after arguments");
      }

      if (const auto* variable = dynamic_cast<const VariableExpr*>(expr.get())) {
        expr = std::make_unique<CallExpr>(variable->name(), std::move(arguments));
        continue;
      }

      if (auto* get = dynamic_cast<GetExpr*>(expr.get())) {
        expr =
            std::make_unique<MethodCallExpr>(get->takeObject(), get->name(), std::move(arguments));
        continue;
      }
    }

    if (match(TokenType::LeftBracket)) {
      ExprPtr index = expression();

      if (!match(TokenType::RightBracket)) {
        report(peek(), "expected ']' after index");
      }

      expr = std::make_unique<IndexExpr>(std::move(expr), std::move(index));
      continue;
    }

    if (match(TokenType::Dot)) {
      if (!check(TokenType::Identifier)) {
        report(peek(), "expected property name after '.'");
        return expr;
      }
      std::string name = std::string(advance().lexeme);

      expr = std::make_unique<GetExpr>(std::move(expr), std::move(name));
      continue;
    }
    break;
  }

  return expr;
}

ExprPtr Parser::primary() {
  if (match(TokenType::Number)) {
    const Token& token = previous();
    return std::make_unique<NumberExpr>(std::string(token.lexeme));
  }

  if (match(TokenType::String)) {
    const Token& token = previous();
    return std::make_unique<StringExpr>(decodeStringLiteral(token.lexeme));
  }

  if (match(TokenType::LeftParen)) {
    ExprPtr expr = expression();

    if (!match(TokenType::RightParen)) {
      report(peek(), "expected ')' after expression");
    }

    return std::make_unique<GroupingExpr>(std::move(expr));
  }

  if (match(TokenType::Identifier)) {
    const Token& token = previous();
    return std::make_unique<VariableExpr>(std::string(token.lexeme));
  }

  if (match(TokenType::Null)) {
    const Token& token = previous();
    return std::make_unique<NullExpr>(std::string(token.lexeme));
  }

  if (match(TokenType::True)) {
    const Token& token = previous();
    return std::make_unique<BoolExpr>(std::string(token.lexeme));
  }

  if (match(TokenType::False)) {
    const Token& token = previous();
    return std::make_unique<BoolExpr>(std::string(token.lexeme));
  }

  if (match(TokenType::LeftBracket)) {
    std::vector<ExprPtr> elements;
    if (!check(TokenType::RightBracket)) {
      do {
        elements.push_back(expression());
      } while (match(TokenType::Comma));
    }

    if (!match(TokenType::RightBracket)) {
      report(peek(), "expected ']' after array literal");
    }
    return std::make_unique<ArrayExpr>(std::move(elements));
  }

  if (match(TokenType::LeftBrace)) {
    std::vector<ObjectProperty> properties;
    if (!check(TokenType::RightBrace)) {
      do {
        if (!check(TokenType::Identifier)) {
          report(peek(), "expected property name");
          break;
        }

        std::string name = std::string(advance().lexeme);

        if (!match(TokenType::Colon)) {
          report(peek(), "expected ':' after property name");
        }

        ExprPtr value = expression();
        properties.push_back(ObjectProperty{name, std::move(value)});
      } while (match(TokenType::Comma));
    }

    if (!match(TokenType::RightBrace)) {
      report(peek(), "expected '}' after object literal");
    }

    return std::make_unique<ObjectExpr>(std::move(properties));
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
