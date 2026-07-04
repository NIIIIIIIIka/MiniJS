#pragma once

#include "minijs/ast.h"
#include "minijs/chunk.h"
#include "minijs/value.h"

namespace minijs {

class Compiler {
 public:
  Chunk compile(const Expr& expression);
  Chunk compileProgram(const Program& program);

 private:
  void emitExpression(const Expr& expression);
  void emitConstant(Value value);
  void emitOpcode(Opcode opcode);
  void emitStatement(const Stmt& statement, bool keepValue);
  void emitGlobalName(const std::string& name, Opcode opcode);
  void emitByte(std::uint8_t byte);
  std::size_t emitJump(Opcode opcode);
  void patchJump(std::size_t offset);
  void emitLoop(std::size_t loopStart);
  Chunk chunk_;
};

}  // namespace minijs
