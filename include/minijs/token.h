#pragma once

#include <string_view>

#include "minijs/source_location.h"

namespace minijs {

// MiniJS 源码中的 token 类型。
enum class TokenType {
  // 单字符分隔符。
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

  // 算术运算符。
  Plus,
  Minus,
  Star,
  Slash,
  Percent,

  // 赋值、比较和逻辑非运算符。
  Equal,
  EqualEqual,
  Bang,
  BangEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  AndAnd,
  OrOr,

  // 字面量和标识符。
  Identifier,
  Number,
  String,
  True,
  False,
  Null,
  Undefined,

  // 保留关键字。
  Let,
  If,
  Else,
  While,
  Function,
  Return,
  Break,
  Continue,

  // 特殊 token。
  Invalid,
  Eof,
};

// 词法分析器输出的单个 token。
struct Token {
  TokenType type;
  std::string_view lexeme;
  SourceLocation location;
};

// 返回 token 类型的调试名称。
std::string_view tokenTypeName(TokenType type);

}  // namespace minijs
