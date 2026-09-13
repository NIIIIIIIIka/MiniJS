#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minijs/chunk.h"

namespace minijs {

struct BaselineCode;

// 编译期 upvalue 描述。
// isLocal=true 表示从直接外层函数的 local slot 捕获；
// isLocal=false 表示复用直接外层闭包的 upvalue slot。
struct UpvalueDescriptor {
  bool isLocal = false;
  std::uint8_t index = 0;
};

enum class JitState : std::uint8_t {
  Cold,
  Scheduled,
  Compiled,
  Failed,
};

struct JitFeedback {
  std::uint32_t callCount = 0;
  std::uint32_t backedgeCount = 0;
  std::uint32_t baselineEntryCount = 0;
  JitState state = JitState::Cold;
  std::shared_ptr<BaselineCode> baselineCode;
  std::string compileError;
};

// 字节码函数对象。函数体拥有自己的 Chunk，参数名用于编译局部槽位和检查参数数量。
struct BytecodeFunction {
  std::string name;
  std::vector<std::string> params;
  Chunk chunk;
  std::vector<UpvalueDescriptor> upvalues;
  JitFeedback jit;
};

}  // namespace minijs
