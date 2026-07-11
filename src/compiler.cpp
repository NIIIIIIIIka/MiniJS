#include "minijs/compiler.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <utility>

#include "minijs/bytecode_function.h"
#include "minijs/runtime_error.h"
#include "minijs/token.h"

namespace minijs {

namespace {

bool producesValue(const Stmt& statement) {
  return dynamic_cast<const ExprStmt*>(&statement) != nullptr;
}

}  // namespace

Compiler::Compiler(Compiler* enclosing) : enclosing_(enclosing) {}

Chunk Compiler::compile(const Expr& expression) {
  chunk_ = Chunk();
  locals_.clear();
  upvalues_.clear();
  scopeDepth_ = 0;
  emitExpression(expression);
  emitOpcode(Opcode::Return);
  return chunk_;
}

Chunk Compiler::compileProgram(const Program& program) {
  chunk_ = Chunk();
  locals_.clear();
  upvalues_.clear();
  scopeDepth_ = 0;

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

  if (const auto* object = dynamic_cast<const ObjectExpr*>(&expression)) {
    if (object->properties().size() > std::numeric_limits<std::uint8_t>::max()) {
      throw RuntimeError("too many properties in bytecode object literal");
    }

    std::vector<Value> names;
    names.reserve(object->properties().size());

    for (const auto& property : object->properties()) {
      emitExpression(*property.value);
      names.push_back(Value(property.name));
    }

    const std::uint8_t namesIndex = addConstant(Value(std::move(names)));

    emitOpcode(Opcode::Object);
    emitByte(namesIndex);
    return;
  }

  if (const auto* get = dynamic_cast<const GetExpr*>(&expression)) {
    emitExpression(get->object());

    const std::uint8_t nameIndex = addConstant(Value(get->name()));
    emitOpcode(Opcode::GetProperty);
    emitByte(nameIndex);
    return;
  }

  if (const auto* set = dynamic_cast<const SetExpr*>(&expression)) {
    emitExpression(set->object());
    emitExpression(set->value());

    const std::uint8_t nameIndex = addConstant(Value(set->name()));
    emitOpcode(Opcode::SetProperty);
    emitByte(nameIndex);
    return;
  }

  if (const auto* methodCall = dynamic_cast<const MethodCallExpr*>(&expression)) {
    emitExpression(methodCall->object());
    if (methodCall->arguments().size() > std::numeric_limits<std::uint8_t>::max()) {
      throw RuntimeError("too many call arguments");
    }
    for (const ExprPtr& argument : methodCall->arguments()) {
      emitExpression(*argument);
    }

    const std::uint8_t nameIndex = addConstant(Value(methodCall->name()));
    emitOpcode(Opcode::MethodCall);
    emitByte(nameIndex);
    emitByte(static_cast<std::uint8_t>(methodCall->arguments().size()));
    return;
  }

  if (const auto* array = dynamic_cast<const ArrayExpr*>(&expression)) {
    if (array->elements().size() > std::numeric_limits<std::uint8_t>::max()) {
      throw RuntimeError("too many elements in bytecode array literal");
    }

    for (const ExprPtr& element : array->elements()) {
      emitExpression(*element);
    }

    emitOpcode(Opcode::Array);
    emitByte(static_cast<std::uint8_t>(array->elements().size()));
    return;
  }

  if (const auto* index = dynamic_cast<const IndexExpr*>(&expression)) {
    emitExpression(index->object());
    emitExpression(index->index());
    emitOpcode(Opcode::GetIndex);
    return;
  }

  if (const auto* indexAssign = dynamic_cast<const IndexAssignExpr*>(&expression)) {
    emitExpression(indexAssign->object());
    emitExpression(indexAssign->index());
    emitExpression(indexAssign->value());
    emitOpcode(Opcode::SetIndex);
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

  if (const auto* call = dynamic_cast<const CallExpr*>(&expression)) {
    emitExpression(call->callee());
    if (call->arguments().size() > std::numeric_limits<std::uint8_t>::max()) {
      throw RuntimeError("too many call arguments");
    }
    for (const ExprPtr& argument : call->arguments()) {
      emitExpression(*argument);
    }
    emitOpcode(Opcode::Call);
    emitByte(static_cast<std::uint8_t>(call->arguments().size()));
    return;
  }

  if (const auto* variable = dynamic_cast<const VariableExpr*>(&expression)) {
    const int slot = resolveLocal(variable->name());
    if (slot >= 0) {
      emitLocal(static_cast<std::uint8_t>(slot), Opcode::GetLocal);
      return;
    }
    if (!currentFunctionName_.empty() && variable->name() == currentFunctionName_) {
      emitOpcode(Opcode::GetCurrentClosure);
      return;
    }
    const int upvalue = resolveUpvalue(variable->name());
    if (upvalue >= 0) {
      emitLocal(static_cast<std::uint8_t>(upvalue), Opcode::GetUpvalue);
      return;
    }
    emitGlobalName(variable->name(), Opcode::GetGlobal);
    return;
  }

  if (const auto* assign = dynamic_cast<const AssignExpr*>(&expression)) {
    emitExpression(assign->value());
    const int slot = resolveLocal(assign->name());
    if (slot >= 0) {
      emitLocal(static_cast<std::uint8_t>(slot), Opcode::SetLocal);
      return;
    }
    const int upvalue = resolveUpvalue(assign->name());
    if (upvalue >= 0) {
      emitLocal(static_cast<std::uint8_t>(upvalue), Opcode::SetUpvalue);
      return;
    }
    emitGlobalName(assign->name(), Opcode::SetGlobal);
    return;
  }
  if (const auto* logical = dynamic_cast<const LogicalExpr*>(&expression)) {
    emitExpression(logical->left());

    if (logical->op() == TokenType::AndAnd) {
      const std::size_t endJump = emitJump(Opcode::JumpIfFalse);
      emitOpcode(Opcode::Pop);
      emitExpression(logical->right());
      patchJump(endJump);
      return;
    }

    if (logical->op() == TokenType::OrOr) {
      const std::size_t elseJump = emitJump(Opcode::JumpIfFalse);
      const std::size_t endJump = emitJump(Opcode::Jump);

      patchJump(elseJump);
      emitOpcode(Opcode::Pop);
      emitExpression(logical->right());

      patchJump(endJump);
      return;
    }
  }

  throw RuntimeError("unsupported bytecode expression");
}

void Compiler::emitConstant(Value value) {
  emitOpcode(Opcode::Constant);
  emitByte(addConstant(std::move(value)));
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

  if (const auto* functionStmt = dynamic_cast<const FunctionStmt*>(&statement)) {
    std::shared_ptr<BytecodeFunction> function = compileFunction(*functionStmt);
    emitOpcode(Opcode::Closure);
    emitByte(addConstant(Value(function)));
    for (const UpvalueDescriptor& upvalue : function->upvalues) {
      emitByte(upvalue.isLocal ? 1 : 0);
      emitByte(upvalue.index);
    }

    if (scopeDepth_ > 0) {
      addLocal(functionStmt->name());
      return;
    }

    emitGlobalName(functionStmt->name(), Opcode::DefineGlobal);
    return;
  }

  if (const auto* returnStmt = dynamic_cast<const ReturnStmt*>(&statement)) {
    if (!isFunction_) {
      throw RuntimeError("return outside function");
    }

    if (returnStmt->value() != nullptr) {
      emitExpression(*returnStmt->value());
    } else {
      emitConstant(Value::undefined());
    }
    emitOpcode(Opcode::Return);
    return;
  }

  if (const auto* letStmt = dynamic_cast<const LetStmt*>(&statement)) {
    emitExpression(letStmt->initializer());
    if (scopeDepth_ > 0) {
      // 局部变量的初始值已经留在栈顶；这里只登记名字到栈槽的映射。
      addLocal(letStmt->name());
      return;
    }
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
    beginScope();
    for (const StmtPtr& inner : blockStmt->statements()) {
      if (inner) {
        emitStatement(*inner, false);
      }
    }
    // 离开块时同步弹出 locals_ 记录和对应的 VM 栈槽。
    endScope();
    return;
  }

  if (const auto* whileStmt = dynamic_cast<const WhileStmt*>(&statement)) {
    const std::size_t loopStart = chunk_.count();
    loops_.push_back(LoopContext{loopStart, {}, {}, false, scopeDepth_});
    emitExpression(whileStmt->condition());
    const std::size_t exitJump = emitJump(Opcode::JumpIfFalse);
    emitOpcode(Opcode::Pop);
    emitStatement(whileStmt->body(), false);
    emitLoop(loopStart);
    patchJump(exitJump);
    emitOpcode(Opcode::Pop);

    // break 的目标是循环后的当前位置，等循环体全部生成后统一回填。
    for (std::size_t jump : loops_.back().breakJumps) {
      patchJump(jump);
    }
    loops_.pop_back();
    return;
  }

  if (const auto* forStmt = dynamic_cast<const ForStmt*>(&statement)) {
    if (forStmt->initializer()) {
      emitStatement(*forStmt->initializer(), false);
    }

    const std::size_t loopStart = chunk_.count();
    loops_.push_back(LoopContext{loopStart, {}, {}, forStmt->increment() != nullptr, scopeDepth_});

    std::size_t exitJump = 0;
    bool hasExitJump = false;

    if (forStmt->condition()) {
      emitExpression(*forStmt->condition());
      exitJump = emitJump(Opcode::JumpIfFalse);
      hasExitJump = true;
      emitOpcode(Opcode::Pop);
    }

    emitStatement(forStmt->body(), false);

    if (forStmt->increment() != nullptr) {
      // for 的 continue 要先跳到 increment；现在 increment 起点已确定，可以回填。
      for (std::size_t jump : loops_.back().continueJumps) {
        patchJump(jump);
      }
      emitExpression(*forStmt->increment());
      emitOpcode(Opcode::Pop);
    }

    emitLoop(loopStart);

    if (hasExitJump) {
      patchJump(exitJump);
      emitOpcode(Opcode::Pop);
    }

    // break 跳过 increment 和循环回跳，直接落到循环结束后的当前位置。
    for (std::size_t jump : loops_.back().breakJumps) {
      patchJump(jump);
    }
    loops_.pop_back();
    return;
  }

  if (dynamic_cast<const ContinueStmt*>(&statement) != nullptr) {
    if (loops_.empty()) {
      throw RuntimeError("continue outside loop");
    }

    // continue 会绕过当前块的 endScope()，所以先弹掉块内局部变量的栈槽。
    emitLocalPopsToDepth(loops_.back().scopeDepth);
    if (loops_.back().continueNeedsPatch) {
      // 带 increment 的 for 还不知道 increment 起点，先记录向前跳转稍后回填。
      const std::size_t jump = emitJump(Opcode::Jump);
      loops_.back().continueJumps.push_back(jump);
      return;
    }

    // while 或无 increment 的 for 可以直接回到循环条件起点。
    emitLoop(loops_.back().continueTarget);
    return;
  }

  if (dynamic_cast<const BreakStmt*>(&statement) != nullptr) {
    if (loops_.empty()) {
      throw RuntimeError("break outside loop");
    }

    // break 同样会绕过块作用域收尾；跳走前保持运行时栈和局部变量布局一致。
    emitLocalPopsToDepth(loops_.back().scopeDepth);
    const std::size_t jump = emitJump(Opcode::Jump);
    loops_.back().breakJumps.push_back(jump);
    return;
  }

  if (const auto* classStmt = dynamic_cast<const ClassStmt*>(&statement)) {
    emitOpcode(Opcode::Class);
    emitByte(addConstant(Value(classStmt->name())));

    const bool isLocalClass = scopeDepth_ > 0;
    if (isLocalClass) {
      addLocal(classStmt->name());
    } else {
      emitGlobalName(classStmt->name(), Opcode::DefineGlobal);
      emitGlobalName(classStmt->name(), Opcode::GetGlobal);
    }

    for (const auto& method : classStmt->methods()) {
      if (!method) {
        continue;
      }

      std::shared_ptr<BytecodeFunction> function = compileFunction(*method, FunctionKind::Method);
      emitOpcode(Opcode::Closure);
      emitByte(addConstant(Value(function)));
      for (const UpvalueDescriptor& upvalue : function->upvalues) {
        emitByte(upvalue.isLocal ? 1 : 0);
        emitByte(upvalue.index);
      }

      emitOpcode(Opcode::Method);
      emitByte(addConstant(Value(method->name())));
    }

    if (!isLocalClass) {
      emitOpcode(Opcode::Pop);
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

std::shared_ptr<BytecodeFunction> Compiler::compileFunction(const FunctionStmt& function,
                                                            FunctionKind kind) {
  auto bytecode_function = std::make_shared<BytecodeFunction>();
  bytecode_function->name = function.name();
  bytecode_function->params = function.params();

  Compiler compiler(this);
  compiler.currentFunctionName_ = function.name();
  compiler.isFunction_ = true;
  compiler.scopeDepth_ = 1;
  if (kind == FunctionKind::Method) {
    // 方法调用时 CallFrame::slotStart 指向 receiver，因此 local 0 就是 this。
    compiler.addLocal("this");
  }
  // 普通函数的参数直接占用函数帧开头的局部槽；方法参数排在 this 后面。
  for (const std::string& param : function.params()) {
    compiler.addLocal(param);
  }

  for (const StmtPtr& statement : function.body()) {
    if (statement) {
      compiler.emitStatement(*statement, false);
    }
  }

  compiler.emitConstant(Value::undefined());
  compiler.emitOpcode(Opcode::Return);
  bytecode_function->upvalues = std::move(compiler.upvalues_);
  bytecode_function->chunk = std::move(compiler.chunk_);
  return bytecode_function;
}

void Compiler::emitByte(std::uint8_t byte) { chunk_.writeByte(byte); }

std::size_t Compiler::emitJump(Opcode opcode) {
  emitOpcode(opcode);
  // 目标位置未知时先写 0xffff，稍后由 patchJump 回填真实偏移。
  emitByte(0xff);
  emitByte(0xff);
  return chunk_.count() - 2;
}

void Compiler::emitLoop(std::size_t loopStart) {
  emitOpcode(Opcode::Loop);

  const std::size_t offset = chunk_.count() - loopStart + 2;
  if (offset > std::numeric_limits<std::uint16_t>::max()) {
    throw RuntimeError("loop body too large");
  }

  emitByte(static_cast<std::uint8_t>((offset >> 8) & 0xff));
  emitByte(static_cast<std::uint8_t>(offset & 0xff));
}

void Compiler::emitLocalPopsToDepth(int depth) {
  for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
    if (it->depth <= depth) {
      break;
    }
    emitOpcode(it->isCaptured ? Opcode::CloseUpvalue : Opcode::Pop);
  }
}

void Compiler::patchJump(std::size_t offset) {
  const std::size_t jump = chunk_.count() - offset - 2;

  if (jump > std::numeric_limits<std::uint16_t>::max()) {
    throw RuntimeError("too much code to jump over");
  }

  chunk_.patchByte(offset, static_cast<std::uint8_t>((jump >> 8) & 0xff));
  chunk_.patchByte(offset + 1, static_cast<std::uint8_t>(jump & 0xff));
}

void Compiler::beginScope() { ++scopeDepth_; }

void Compiler::endScope() {
  --scopeDepth_;

  // locals_ 和运行时栈按同一顺序增长；退出作用域时也必须同步回退。
  while (!locals_.empty() && locals_.back().depth > scopeDepth_) {
    emitOpcode(locals_.back().isCaptured ? Opcode::CloseUpvalue : Opcode::Pop);
    locals_.pop_back();
  }
}

void Compiler::addLocal(const std::string& name) {
  for (auto it = locals_.rbegin(); it != locals_.rend(); ++it) {
    if (it->depth < scopeDepth_) {
      break;
    }
    if (it->name == name) {
      throw RuntimeError("variable already declared in this scope: " + name);
    }
  }

  if (locals_.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw RuntimeError("too many local variables");
  }

  locals_.push_back(Local{name, scopeDepth_, false});
}

int Compiler::resolveLocal(const std::string& name) const {
  // 从后往前查找，优先命中内层作用域的同名变量。
  for (std::size_t i = locals_.size(); i > 0; --i) {
    if (locals_[i - 1].name == name) {
      return static_cast<int>(i - 1);
    }
  }
  return -1;
}

// 查找当前函数引用的外层变量。
// 如果直接外层有 local，则记录为 local upvalue；
// 否则递归询问外层 compiler，让多层闭包通过 upvalue 链转发。
int Compiler::resolveUpvalue(const std::string& name) {
  if (enclosing_ == nullptr) {
    return -1;
  }

  const int local = enclosing_->resolveLocal(name);
  if (local >= 0) {
    enclosing_->locals_[static_cast<std::size_t>(local)].isCaptured = true;
    return addUpvalue(true, static_cast<std::uint8_t>(local));
  }

  const int upvalue = enclosing_->resolveUpvalue(name);
  if (upvalue >= 0) {
    return addUpvalue(false, static_cast<std::uint8_t>(upvalue));
  }

  return -1;
}

// 在当前函数的 upvalue 表中登记捕获来源；同一来源只保留一份。
std::uint8_t Compiler::addUpvalue(bool isLocal, std::uint8_t index) {
  for (std::size_t i = 0; i < upvalues_.size(); ++i) {
    if (upvalues_[i].isLocal == isLocal && upvalues_[i].index == index) {
      return static_cast<std::uint8_t>(i);
    }
  }

  if (upvalues_.size() > std::numeric_limits<std::uint8_t>::max()) {
    throw RuntimeError("too many closure variables");
  }

  upvalues_.push_back(UpvalueDescriptor{isLocal, index});
  return static_cast<std::uint8_t>(upvalues_.size() - 1);
}

std::uint8_t Compiler::addConstant(Value value) {
  const std::size_t index = chunk_.addConstant(std::move(value));
  if (index > std::numeric_limits<std::uint8_t>::max()) {
    throw RuntimeError("too many constants in bytecode chunk");
  }
  return static_cast<std::uint8_t>(index);
}

void Compiler::emitLocal(std::uint8_t slot, Opcode opcode) {
  emitOpcode(opcode);
  emitByte(slot);
}

}  // namespace minijs
