#pragma once

#include "minijs/ast.h"
#include "minijs/chunk.h"
#include "minijs/value.h"

namespace minijs {

class Compiler {
 public:
  Chunk compile(const Expr& expression);

 private:
  void emitExpression(const Expr& expression);
  void emitConstant(Value value);
  void emitOpcode(Opcode opcode);

  Chunk chunk_;
};

}  // namespace minijs
