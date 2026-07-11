#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "minijs/bytecode_function.h"
#include "minijs/value.h"

namespace minijs {

// 捕获的外部局部变量。
// 变量仍在外层调用帧栈槽中时，stackIndex 指向该槽；
// 当作用域结束或函数返回时，值会复制到 closed，并将 isClosed 置为 true。
struct Upvalue {
  std::size_t stackIndex = 0;
  Value closed;
  bool isClosed = false;
};

// 运行时闭包 = 函数代码模板 + 本次创建时捕获到的 upvalue。
// 同一个 BytecodeFunction 可以被多次执行并创建多个闭包实例。
struct BytecodeClosure {
  std::shared_ptr<BytecodeFunction> function;
  std::vector<std::shared_ptr<Upvalue>> upvalues;
};

}  // namespace minijs
