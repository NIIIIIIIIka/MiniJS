#include "minijs/compiler.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "minijs/runtime_error.h"
#include "minijs/token.h"

namespace minijs {
namespace {

bool producesValue(const Stmt& statement) {
  return dynamic_cast<const ExprStmt*>(&statement) != nullptr;
}

}  // namespace

Chunk Compiler::compile(const Expr& expression) {
  chunk_ = Chunk();
  emitExpression(expression);
  emitOpcode(Opcode::Return);
  return chunk_;
}

Chunk Compiler::compileProgram(const Program& program) {
  chunk_ = Chunk();

  std::size_t last_value_statement = program.size();
  for (std::size_t i = 0; i < program.size(); ++i) {
    if (program[i] && producesValue(*program[i])) {
      last_value_statement = i;
    }
  }

  for (std::size_t i = 0; i < program.size(); ++i) {
    if (program[i]) {
      emitStatement(*program[i], i == last_value_statement);
    }
  }

  if (last_value_statement == program.size()) {
    emitConstant(Value());
  }
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
    if (unary->op() == TokenType::Bang) {
      emitOpcode(Opcode::Not);
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
      case TokenType::EqualEqual:
        emitOpcode(Opcode::Equal);
        return;

      case TokenType::BangEqual:
        emitOpcode(Opcode::Equal);
        emitOpcode(Opcode::Not);
        return;

      case TokenType::Greater:
        emitOpcode(Opcode::Greater);
        return;

      case TokenType::GreaterEqual:
        emitOpcode(Opcode::Less);
        emitOpcode(Opcode::Not);
        return;

      case TokenType::Less:
        emitOpcode(Opcode::Less);
        return;

      case TokenType::LessEqual:
        emitOpcode(Opcode::Greater);
        emitOpcode(Opcode::Not);
        return;
      default:
        throw RuntimeError("unsupported bytecode binary operator");
    }
  }

  if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
    emitGlobalName(variable->name(), Opcode::GetGlobal);
    return;
  }
  if (const auto* assign = dynamic_cast<const AssignExpr*>(&expression)) {
    emitExpression(assign->value());
    emitGlobalName(assign->name(), Opcode::SetGlobal);
    return;
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

void Compiler::emitStatement(const Stmt& statement, bool keepValue) {
  if (const auto* exprStmt = dynamic_cast<const ExprStmt*>(&statement)) {
    emitExpression(exprStmt->expression());
    if (!keepValue) {
      emitOpcode(Opcode::Pop);
    }
    return;
  }
  if (const auto* letStmt = dynamic_cast<const LetStmt*>(&statement)) {
    emitExpression(letStmt->initializer());
    emitGlobalName(letStmt->name(), Opcode::DefineGlobal);
    return;
  }

  if (const auto* ifStmt = dynamic_cast<const IfStmt*>(&statement)) {
    emitExpression(ifStmt->condition());

    const std::size_t thenJump = emitJump(Opcode::JumpIfFalse);
    emitOpcode(Opcode::Pop);

    emitStatement(ifStmt->thenBranch(), keepValue);

    const std::size_t elseJump = emitJump(Opcode::Jump);

    patchJump(thenJump);
    emitOpcode(Opcode::Pop);

    if (ifStmt->elseBranch() != nullptr) {
      emitStatement(*ifStmt->elseBranch(), keepValue);
    }

    patchJump(elseJump);
    return;
  }
  if (const auto* blockStmt = dynamic_cast<const BlockStmt*>(&statement)) {
    for (const StmtPtr& inner : blockStmt->statements()) {
      if (inner) {
        emitStatement(*inner, false);
      }
    }
    return;
  }
  throw RuntimeError("unsupported bytecode statement");
}

void Compiler::emitGlobalName(const std::string& name, Opcode opcode) {
  const std::size_t index = chunk_.addConstant(Value(std::string(name)));
  if (index > std::numeric_limits<std::uint8_t>::max()) {
    throw RuntimeError("too many constants in bytecode chunk");
  }
  emitOpcode(opcode);
  chunk_.writeByte(static_cast<std::uint8_t>(index));
}

void Compiler::emitByte(std::uint8_t byte) { chunk_.writeByte(byte); }

std::size_t Compiler::emitJump(Opcode opcode) {
  emitOpcode(opcode);
  emitByte(0xff);
  emitByte(0xff);
  return chunk_.count() - 2;
}

void Compiler::patchJump(std::size_t offset) {
  const std::size_t jump = chunk_.count() - offset - 2;

  if (jump > std::numeric_limits<std::uint16_t>::max()) {
    throw RuntimeError("too much code to jump over");
  }

  chunk_.patchByte(offset, static_cast<std::uint8_t>((jump >> 8) & 0xff));
  chunk_.patchByte(offset + 1, static_cast<std::uint8_t>(jump & 0xff));
}
}  // namespace minijs
