#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minijs/ast.h"
#include "minijs/bytecode_function.h"
#include "minijs/chunk.h"
#include "minijs/value.h"

namespace minijs {

// 编译期局部变量记录。真正的运行期值保存在 VM 栈的同编号槽位。
struct Local {
  std::string name;
  int depth;
  bool isCaptured = false;
};

// 编译循环时保存的跳转上下文。
// break 总是向前跳到循环之后；continue 的目标则取决于循环形态。
struct LoopContext {
  // while 的 continue 可以直接跳回条件起点。
  std::size_t continueTarget = 0;
  // break 的目标位置要等循环结束后才知道，因此先记录待回填跳转。
  std::vector<std::size_t> breakJumps;
  // for 带 increment 时，continue 要先跳到 increment 起点，位置稍后回填。
  std::vector<std::size_t> continueJumps;
  // true 表示 continue 不能直接向后跳，需要先生成待回填的向前跳转。
  bool continueNeedsPatch = false;
  // 进入循环时的作用域深度，用于 break/continue 跳走前清理块内局部变量。
  int scopeDepth = 0;
};

enum class FunctionKind {
  Function,
  Method,
};

// 将 AST 编译为栈式 VM 字节码。
class Compiler {
 public:
  explicit Compiler(Compiler* enclosing = nullptr);

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

  std::shared_ptr<BytecodeFunction> compileFunction(
      const FunctionStmt& function, FunctionKind kind = FunctionKind::Function);
  void emitByte(std::uint8_t byte);
  // 写入向前跳转指令和两个占位字节，返回待回填位置。
  std::size_t emitJump(Opcode opcode);
  void patchJump(std::size_t offset);
  // 写入向后跳转，用于循环回到 loopStart。
  void emitLoop(std::size_t loopStart);
  // 只生成 Pop 指令，不修改 locals_；用于 break/continue 绕过 endScope() 前清理运行时栈。
  void emitLocalPopsToDepth(int depth);
  void beginScope();
  void endScope();
  // 记录一个局部变量；其初始值已经位于 VM 栈顶。
  void addLocal(const std::string& name);
  // 从内向外查找局部变量，返回 VM 栈槽编号；找不到返回 -1。
  int resolveLocal(const std::string& name) const;
  // 查找一个变量，看它是否来自外部作用域，如果是，就记录下来（捕获它）
  int resolveUpvalue(const std::string& name);
  std::uint8_t addUpvalue(bool isLocal, std::uint8_t index);
  void emitLocal(std::uint8_t slot, Opcode opcode);

  std::uint8_t addConstant(Value value);
  Chunk chunk_;
  // locals_ 的下标就是局部变量在当前 VM 栈帧中的槽位。
  std::string currentFunctionName_;
  std::vector<Local> locals_;
  std::vector<LoopContext> loops_;
  std::vector<UpvalueDescriptor> upvalues_;
  Compiler* enclosing_ = nullptr;
  bool isFunction_ = false;
  int scopeDepth_ = 0;
};

}  // namespace minijs
