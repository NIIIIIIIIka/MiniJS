#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "minijs/bytecode_function.h"
#include "minijs/chunk.h"
#include "minijs/value.h"

namespace minijs {

// 一次字节码函数调用的执行状态。
struct CallFrame {
  std::shared_ptr<BytecodeFunction> function;
  std::size_t ip;
  std::size_t slotStart;
};

// 执行 Chunk 的栈式虚拟机。
class VM {
 public:
  VM();

  Value run(const Chunk& chunk);

 private:
  void defineBuiltin(std::string name, std::size_t arity, NativeFn function);

  void push(Value value);
  Value pop();
  const Value& peek() const;

  // 操作数栈，同时承载当前调用帧的参数和局部变量槽位。
  std::vector<Value> stack_;
  std::vector<CallFrame> frames_;
  std::unordered_map<std::string, Value> globals_;
};

}  // namespace minijs
