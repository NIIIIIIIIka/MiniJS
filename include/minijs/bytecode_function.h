#pragma once

#include <string>
#include <vector>

#include "minijs/chunk.h"

namespace minijs {

// 编译期 upvalue 描述。
// isLocal=true 表示从直接外层函数的 local slot 捕获；
// isLocal=false 表示复用直接外层闭包的 upvalue slot。
struct UpvalueDescriptor {
  bool isLocal = false;
  std::uint8_t index = 0;
};

// 字节码函数对象。函数体拥有自己的 Chunk，参数名用于编译局部槽位和检查参数数量。
struct BytecodeFunction {
  std::string name;
  std::vector<std::string> params;
  Chunk chunk;
  std::vector<UpvalueDescriptor> upvalues;
};

}  // namespace minijs
