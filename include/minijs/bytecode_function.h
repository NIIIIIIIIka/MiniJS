#pragma once

#include <string>
#include <vector>

#include "minijs/chunk.h"

namespace minijs {

// 字节码函数对象。函数体拥有自己的 Chunk，参数名用于编译局部槽位和检查参数数量。
struct BytecodeFunction {
  std::string name;
  std::vector<std::string> params;
  Chunk chunk;
};

}  // namespace minijs
