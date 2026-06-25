#pragma once

#include "minijs/ast.h"
#include "minijs/environment.h"
#include "minijs/value.h"

namespace minijs {

class Interpreter {
 public:
  Value interpret(const Program& program);

 private:
  void execute(const Stmt& statement);
  Value evaluate(const Expr& expression);

  Environment environment_;
  Value lastValue_;
};

}  // namespace minijs
