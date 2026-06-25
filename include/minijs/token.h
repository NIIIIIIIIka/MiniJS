#pragma once

#include <string_view>

#include "minijs/source_location.h"

namespace minijs {
enum class TokenType {
  // Single-character punctuation.
  LeftParen,
  RightParen,
  LeftBracket,
  RightBracket,
  LeftBrace,
  RightBrace,
  Comma,
  Dot,
  Semicolon,
  Colon,

  // Arithmetic operators.
  Plus,
  Minus,
  Star,
  Slash,
  Percent,

  // Assignment, comparison, and logical-not operators.
  Equal,
  EqualEqual,
  Bang,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,

  // Literals.
  Identifier,
  Number,
  String,
  True,
  False,
  Null,
  Undefined,

  // Reserved words.
  Let,
  If,
  Else,
  While,
  Function,
  Return,

  Invalid,
  Eof
};

struct Token {
  TokenType type;
  std::string_view lexeme;
  SourceLocation location;
};

std::string_view tokenTypeName(TokenType type);

}  // namespace minijs
