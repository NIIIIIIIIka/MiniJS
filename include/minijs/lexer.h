#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "minijs/diagnostic.h"
#include "minijs/source_location.h"
#include "minijs/token.h"

namespace minijs {

// 词法分析器，将源码切分为 MiniJS token 流。
class Lexer {
 public:
  explicit Lexer(std::string_view source);

  // 兼容旧调用方；新代码优先使用 nextToken()。
  Token next();

  // 返回下一个 token，并推进词法分析器状态。
  Token nextToken();

  // 返回词法分析阶段产生的诊断信息。
  const std::vector<Diagnostic>& diagnostics() const;

 private:
  // 返回是否已经读到源码末尾。
  bool isAtEnd() const;

  // 消费当前字符，并更新源码位置。
  char advance();

  // 查看当前字符但不消费。
  char peek() const;

  // 查看下一个字符但不消费。
  char peekNext() const;

  // 跳过空白字符。
  void skipWhitespace();

  // 基于当前扫描区间创建 token。
  Token makeToken(TokenType type, std::size_t start, SourceLocation startLocation) const;

  // 扫描标识符或关键字。
  Token scanIdentifier(std::size_t start, SourceLocation startLocation);

  // 扫描数字字面量。
  Token scanNumber(std::size_t start, SourceLocation startLocation);

  // 扫描字符串字面量。
  Token scanString(std::size_t start, SourceLocation startLocation);

  // 当前字符等于 expected 时消费它。
  bool match(char expected);

  // 判断字符是否可以作为标识符开头。
  static bool isIdentifierStart(char ch);

  // 判断字符是否可以作为标识符后续部分。
  static bool isIdentifierPart(char ch);

  // 判断字符是否为十进制数字。
  static bool isDigit(char ch);

  // 记录一条词法诊断信息。
  void report(SourceLocation location, std::string message);

  std::string_view source_;
  std::size_t current_ = 0;  // 下一个待读取字符的下标。
  SourceLocation location_;
  std::vector<Diagnostic> diagnostics_;
};

}  // namespace minijs
