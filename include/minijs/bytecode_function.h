#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "minijs/chunk.h"

namespace minijs {

struct BaselineCode;
struct OptimizedCode;

// 编译期 upvalue 描述。
// isLocal=true 表示从直接外层函数的 local slot 捕获；
// isLocal=false 表示复用直接外层闭包的 upvalue slot。
struct UpvalueDescriptor {
  bool isLocal = false;
  std::uint8_t index = 0;
};

enum class JitState : std::uint8_t {
  Cold,       // 尚未达到热点阈值
  Scheduled,  // 已经决定编译，是当前同步实现中的瞬时状态
  Compiled,   // Baseline 编译成功
  Failed,     // native 和 decoded 两条编译路径都失败。
};

enum class ObservedType : std::uint8_t {
  Uninitialized,
  Number,
  Boolean,
  Object,
  Other,
  Polymorphic,
};

enum class OptimizeState : std::uint8_t {
  Cold,
  Scheduled,
  Compiled,
  Disabled,
  Failed,
};

struct OptimizeFeedback {
  std::vector<ObservedType> parameterTypes;
  std::uint32_t compileAttempts = 0;
  std::uint32_t optimizedEntryCount = 0;
  std::uint32_t guardFailures = 0;
  OptimizeState state = OptimizeState::Cold;
  std::shared_ptr<OptimizedCode> code;
  std::string compileError;
};

struct JitFeedback {
  std::uint32_t callCount = 0;
  std::uint32_t backedgeCount = 0;
  std::uint32_t baselineEntryCount = 0;
  JitState state = JitState::Cold;
  std::shared_ptr<BaselineCode> baselineCode;
  std::string compileError;
  OptimizeFeedback optimize;
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
