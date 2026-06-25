#include "minijs/lexer.h"

#include <unordered_map>
#include <utility>

namespace minijs {
namespace {
const std::unordered_map<std::string_view, TokenType> keywordMap = {
    {"while", TokenType::While},
    {"let", TokenType::Let},
    {"if", TokenType::If},
    {"else", TokenType::Else},
    {"function", TokenType::Function},
    {"return", TokenType::Return},
    {"true", TokenType::True},
    {"false", TokenType::False},
    {"null", TokenType::Null},
    {"undefined", TokenType::Undefined},
};
}

Lexer::Lexer(std::string_view source) : source_(source) {}

bool Lexer::isAtEnd() const { return current_ >= source_.size(); }

char Lexer::advance() {
  const char ch = source_[current_++];
  location_.offset = current_;

  if (ch == '\n') {
    ++location_.line;
    location_.column = 1;
  } else {
    ++location_.column;
  }

  return ch;
}

char Lexer::peek() const {
  if (isAtEnd()) {
    return '\0';
  }
  return source_[current_];
}

char Lexer::peekNext() const {
  //TODO: 是否需要考虑越界问题？
  if (current_ + 1 >= source_.size()) {
    return '\0';
  }
  return source_[current_ + 1];
}

void Lexer::skipWhitespace() {
  while (!isAtEnd()) {
    switch (peek()) {
      case ' ':
      case '\t':
      case '\r':
      case '\n':
        advance();
        break;

      default:
        return;
    }
  }
}

Token Lexer::makeToken(TokenType type, std::size_t start, SourceLocation startLocation) const {
  return Token{
      type,
      source_.substr(start, current_ - start),
      startLocation,
  };
}

Token Lexer::next() { return nextToken(); }

const std::vector<Diagnostic>& Lexer::diagnostics() const { return diagnostics_; }

Token Lexer::nextToken() {
  skipWhitespace();

  const std::size_t start = current_;
  const SourceLocation startLocation = location_;

  if (isAtEnd()) {
    return makeToken(TokenType::Eof, start, startLocation);
  }

  const char ch = advance();

  if (isIdentifierStart(ch)) {
    return scanIdentifier(start, startLocation);
  }

  if (isDigit(ch)) {
    return scanNumber(start, startLocation);
  }

  switch (ch) {
    case '(':
      return makeToken(TokenType::LeftParen, start, startLocation);
    case ')':
      return makeToken(TokenType::RightParen, start, startLocation);
    case '[':
      return makeToken(TokenType::LeftBracket, start, startLocation);
    case ']':
      return makeToken(TokenType::RightBracket, start, startLocation);
    case '{':
      return makeToken(TokenType::LeftBrace, start, startLocation);
    case '}':
      return makeToken(TokenType::RightBrace, start, startLocation);
    case ',':
      return makeToken(TokenType::Comma, start, startLocation);
    case '.':
      return makeToken(TokenType::Dot, start, startLocation);
    case ';':
      return makeToken(TokenType::Semicolon, start, startLocation);
    case ':':
      return makeToken(TokenType::Colon, start, startLocation);
    case '+':
      return makeToken(TokenType::Plus, start, startLocation);
    case '-':
      return makeToken(TokenType::Minus, start, startLocation);
    case '*':
      return makeToken(TokenType::Star, start, startLocation);
    case '%':
      return makeToken(TokenType::Percent, start, startLocation);
    case '/':
      //TODO:
      if (match('/')) {
        while (!isAtEnd() && peek() != '\n') {
          advance();
        }
        return nextToken();
      }
      return makeToken(TokenType::Slash, start, startLocation);
      //TODO:
    case '=':
      if (match('=')) {
        return makeToken(TokenType::EqualEqual, start, startLocation);
      }
      return makeToken(TokenType::Equal, start, startLocation);
    case '!':
      if (match('=')) {
        return makeToken(TokenType::BangEqual, start, startLocation);
      }
      return makeToken(TokenType::Bang, start, startLocation);
    case '<':
      if (match('=')) {
        return makeToken(TokenType::LessEqual, start, startLocation);
      }
      return makeToken(TokenType::Less, start, startLocation);
    case '>':
      if (match('=')) {
        return makeToken(TokenType::GreaterEqual, start, startLocation);
      }
      return makeToken(TokenType::Greater, start, startLocation);
    case '"':
      return scanString(start, startLocation);
    default:
      report(startLocation, "unexpected character");
      return makeToken(TokenType::Invalid, start, startLocation);
  }
}

Token Lexer::scanIdentifier(std::size_t start, SourceLocation startLocation) {
  while (isIdentifierPart(peek())) {
    advance();
  }

  const std::string_view text = source_.substr(start, current_ - start);
  const auto it = keywordMap.find(text);
  const TokenType type = it == keywordMap.end() ? TokenType::Identifier : it->second;
  return makeToken(type, start, startLocation);
}

Token Lexer::scanNumber(std::size_t start, SourceLocation startLocation) {
  while (isDigit(peek())) {
    advance();
  }

  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek())) {
      advance();
    }
  }

  return makeToken(TokenType::Number, start, startLocation);
}

Token Lexer::scanString(std::size_t start, SourceLocation startLocation) {
  while (!isAtEnd() && peek() != '"') {
    if (peek() == '\n') {
      report(startLocation, "unterminated string");
      return makeToken(TokenType::Invalid, start, startLocation);
    }

    if (peek() == '\\' && peekNext() != '\0') {
      advance();
    }
    advance();
  }

  if (isAtEnd()) {
    report(startLocation, "unterminated string");
    return makeToken(TokenType::Invalid, start, startLocation);
  }

  advance();
  return makeToken(TokenType::String, start, startLocation);
}

bool Lexer::match(char expected) {
  if (peek() == expected) {
    advance();
    return true;
  }
  return false;
}

bool Lexer::isIdentifierStart(char ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

bool Lexer::isIdentifierPart(char ch) { return isIdentifierStart(ch) || isDigit(ch); }

bool Lexer::isDigit(char ch) { return ch >= '0' && ch <= '9'; }

void Lexer::report(SourceLocation location, std::string message) {
  diagnostics_.emplace_back(location, std::move(message));
}

}  // namespace minijs
