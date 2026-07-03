#include "minijs/compiler.h"

#include <cstdint>
#include <limits>

#include "minijs/runtime_error.h"
#include "minijs/token.h"

namespace minijs {

Chunk Compiler::compile(const Expr& expression) {
  chunk_ = Chunk();
  emitExpression(expression);
  emitOpcode(Opcode::Return);
  return chunk_;
}

void Compiler::emitExpression(const Expr& expression) {
  if (const auto* number = dynamic_cast<const NumberExpr*>(&expression)) {
    emitConstant(Value(std::stod(number->value())));
    return;
  }

  if (const auto* grouping = dynamic_cast<const GroupingExpr*>(&expression)) {
    emitExpression(grouping->expression());
    return;
  }
  if (const auto* string = dynamic_cast<const StringExpr*>(&expression)) {
    emitConstant(Value(string->value()));
    return;
  }

  if (const auto* unary = dynamic_cast<const UnaryExpr*>(&expression)) {
    emitExpression(unary->right());
    if (unary->op() == TokenType::Minus) {
      emitOpcode(Opcode::Negate);
      return;
    }
    throw RuntimeError("unsupported bytecode unary operator");
  }

  if (const auto* boolean = dynamic_cast<const BoolExpr*>(&expression)) {
    emitConstant(Value(boolean->value() == "true"));
    return;
  }

  if (dynamic_cast<const NullExpr*>(&expression) != nullptr) {
    emitConstant(Value());
    return;
  }

  if (dynamic_cast<const UndefinedExpr*>(&expression) != nullptr) {
    emitConstant(Value::undefined());
    return;
  }

  if (const auto* binary = dynamic_cast<const BinaryExpr*>(&expression)) {
    emitExpression(binary->left());
    emitExpression(binary->right());

    switch (binary->op()) {
      case TokenType::Plus:
        emitOpcode(Opcode::Add);
        return;
      case TokenType::Minus:
        emitOpcode(Opcode::Sub);
        return;
      case TokenType::Star:
        emitOpcode(Opcode::Mul);
        return;
      case TokenType::Slash:
        emitOpcode(Opcode::Div);
        return;
      case TokenType::Percent:
        emitOpcode(Opcode::Mod);
        return;
      default:
        throw RuntimeError("unsupported bytecode binary operator");
    }
  }

  throw RuntimeError("unsupported bytecode expression");
}

void Compiler::emitConstant(Value value) {
  const std::size_t index = chunk_.addConstant(std::move(value));
  if (index > std::numeric_limits<std::uint8_t>::max()) {
    throw RuntimeError("too many constants in bytecode chunk");
  }

  emitOpcode(Opcode::Constant);
  chunk_.writeByte(static_cast<std::uint8_t>(index));
}

void Compiler::emitOpcode(Opcode opcode) { chunk_.writeOpcode(opcode); }

}  // namespace minijs
