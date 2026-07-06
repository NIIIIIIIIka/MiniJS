#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minijs/ast.h"
#include "minijs/chunk.h"
#include "minijs/value.h"

namespace minijs {

struct BytecodeFunction;

// 编译期局部变量记录。真正的运行期值保存在 VM 栈的同编号槽位。
struct Local {
  std::string name;
  int depth;
};

// 将 AST 编译为栈式 VM 字节码。
class Compiler {
 public:
  // 编译单个表达式，主要用于表达式级测试。
  Chunk compile(const Expr& expression);
  // 编译完整程序，保留最后一个表达式语句作为返回值。
  Chunk compileProgram(const Program& program);

 private:
  void emitExpression(const Expr& expression);
  void emitConstant(Value value);
  void emitOpcode(Opcode opcode);
  void emitStatement(const Stmt& statement, bool keepValue);
  void emitGlobalName(const std::string& name, Opcode opcode);
  std::shared_ptr<BytecodeFunction> compileFunction(const FunctionStmt& function);
  void emitByte(std::uint8_t byte);
  // 写入向前跳转指令和两个占位字节，返回待回填位置。
  std::size_t emitJump(Opcode opcode);
  void patchJump(std::size_t offset);
  // 写入向后跳转，用于循环回到 loopStart。
  void emitLoop(std::size_t loopStart);
  void beginScope();
  void endScope();
  // 记录一个局部变量；其初始值已经位于 VM 栈顶。
  void addLocal(const std::string& name);
  // 从内向外查找局部变量，返回 VM 栈槽编号；找不到返回 -1。
  int resolveLocal(const std::string& name) const;
  void emitLocal(std::uint8_t slot, Opcode opcode);

  Chunk chunk_;
  // locals_ 的下标就是局部变量在当前 VM 栈帧中的槽位。
  std::vector<Local> locals_;
  int scopeDepth_ = 0;
};

}  // namespace minijs
