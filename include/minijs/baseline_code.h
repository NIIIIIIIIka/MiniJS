#pragma once

#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#include "minijs/baseline_frame.h"
#include "minijs/bytecode_decoder.h"
#include "minijs/executable_memory.h"

namespace minijs {

// Baseline 复用 VM 字节码，不维护第二套 opcode。这里缓存 verifier 认可后的
// decoded bytecode 和 offset 映射，后续 native codegen 也以这些指令为输入。
struct BaselineCode {
  // verifier 和 decoder 产出的结构化指令序列。
  // decoded executor 直接解释它；native backend 也按它逐条生成机器码。
  std::vector<DecodedInstruction> instructions;

  // bytecode offset -> instructions 下标。
  // decoded executor 处理跳转时用它把 OP_JUMP/OP_LOOP 的目标 offset
  // 转成可执行的指令数组下标。不是每个 bytecode offset 都是指令起点，
  // 非指令起点会保持为 kInvalidInstructionIndex。
  std::vector<std::size_t> bytecodeOffsetToInstructionIndex;

  // bytecode offset -> native code offset。
  // native backend 生成机器码时记录每条 bytecode 指令对应的机器码起点。
  // 当前主要用于测试和调试；后续 safepoint、deopt、profiling 也可以复用它。
  std::vector<std::size_t> bytecodeOffsetToNativeOffset;

  // 持有真正的可执行内存。ExecutableMemory 负责 RW 写入、切换 RX、
  // 刷新 instruction cache，以及析构时释放平台相关内存。
  std::shared_ptr<ExecutableMemory> executableMemory;

  // native baseline 入口函数。支持当前平台和 opcode 子集时指向 executableMemory
  // 中的机器码；没有 native entry 时保持 nullptr，并回退到 decoded executor。
  BaselineEntry entry = nullptr;
};

constexpr std::size_t kInvalidInstructionIndex = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kInvalidNativeOffset = std::numeric_limits<std::size_t>::max();

}  // namespace minijs
