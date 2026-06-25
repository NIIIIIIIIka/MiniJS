#include "minijs/token.h"

namespace minijs {

std::string_view tokenTypeName(TokenType type) {
  switch (type) {
    case TokenType::LeftParen:
      return "LeftParen";
    case TokenType::RightParen:
      return "RightParen";
    case TokenType::LeftBracket:
      return "LeftBracket";
    case TokenType::RightBracket:
      return "RightBracket";
    case TokenType::LeftBrace:
      return "LeftBrace";
    case TokenType::RightBrace:
      return "RightBrace";
    case TokenType::Comma:
      return "Comma";
    case TokenType::Dot:
      return "Dot";
    case TokenType::Semicolon:
      return "Semicolon";
    case TokenType::Colon:
      return "Colon";
    case TokenType::Plus:
      return "Plus";
    case TokenType::Minus:
      return "Minus";
    case TokenType::Star:
      return "Star";
    case TokenType::Slash:
      return "Slash";
    case TokenType::Percent:
      return "Percent";
    case TokenType::Equal:
      return "Equal";
    case TokenType::EqualEqual:
      return "EqualEqual";
    case TokenType::Bang:
      return "Bang";
    case TokenType::BangEqual:
      return "BangEqual";
    case TokenType::Less:
      return "Less";
    case TokenType::LessEqual:
      return "LessEqual";
    case TokenType::Greater:
      return "Greater";
    case TokenType::GreaterEqual:
      return "GreaterEqual";
    case TokenType::Identifier:
      return "Identifier";
    case TokenType::Number:
      return "Number";
    case TokenType::String:
      return "String";
    case TokenType::True:
      return "True";
    case TokenType::False:
      return "False";
    case TokenType::Null:
      return "Null";
    case TokenType::Undefined:
      return "Undefined";
    case TokenType::Let:
      return "Let";
    case TokenType::If:
      return "If";
    case TokenType::Else:
      return "Else";
    case TokenType::While:
      return "While";
    case TokenType::Function:
      return "Function";
    case TokenType::Return:
      return "Return";
    case TokenType::Invalid:
      return "Invalid";
    case TokenType::Eof:
      return "Eof";
  }

  return "Unknown";
}

}  // namespace minijs
