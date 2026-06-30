#pragma once

#include "minijs/ast.h"
#include "minijs/environment.h"
#include "minijs/value.h"

namespace minijs {

// 基于 AST 的树遍历解释器。
class Interpreter {
 public:
  // 执行完整程序，并返回最后一条表达式语句的值。
  Value interpret(const Program& program);

 private:
  // 执行一条语句，主要产生环境变化或控制流效果。
  void execute(const Stmt& statement);

  // 求值一个表达式，并返回运行时值。
  Value evaluate(const Expr& expression);

  // 在指定环境中执行语句列表，不改变调用方可见的最后结果语义。
  void executeBlock(const Program& statements, Environment* environment);

  Environment global_environment_;
  Environment* environment_ = &global_environment_;
  Value lastValue_;
};

}  // namespace minijs
