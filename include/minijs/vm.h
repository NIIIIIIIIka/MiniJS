#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "minijs/bytecode_function.h"
#include "minijs/bytecode_closure.h"
#include "minijs/chunk.h"
#include "minijs/value.h"

namespace minijs {

// 一次字节码函数调用的执行状态。
struct CallFrame {
  std::shared_ptr<BytecodeClosure> closure;
  std::size_t ip;
  // 函数返回值要写回的栈槽；普通调用是 callee 槽，方法调用是 receiver/this 槽。
  std::size_t returnSlot;
  // 当前函数局部变量区在 VM 栈中的起点。
  // 调用字节码函数时它指向第一个实参，因此参数 slot 0/1/... 可直接复用栈上的实参。
  // 调用字节码方法时它指向 receiver，因此 local 0 就是 this。
  std::size_t slotStart;
  // 构造器 init 专用：忽略函数体返回值，改为返回 local 0 的 receiver。
  bool returnsReceiver = false;
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
  void callBytecodeClosure(std::shared_ptr<BytecodeClosure> closure, std::size_t argCount,
                           std::size_t returnSlot, std::size_t slotStart, const std::string& label,
                           bool returnsReceiver = false);
  std::shared_ptr<Upvalue> captureUpvalue(std::size_t stackIndex);
  void closeUpvalues(std::size_t firstStackIndex);

  // 操作数栈，同时承载当前调用帧的参数和局部变量槽位。
  std::vector<Value> stack_;
  std::vector<CallFrame> frames_;
  std::vector<std::shared_ptr<Upvalue>> openUpvalues_;
  std::unordered_map<std::string, Value> globals_;
};

}  // namespace minijs
