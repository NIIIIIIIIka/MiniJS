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
  std::vector<DecodedInstruction> instructions;
  std::vector<std::size_t> bytecodeOffsetToInstructionIndex;

  std::vector<std::size_t> bytecodeOffsetToNativeOffset;
  std::shared_ptr<ExecutableMemory> executableMemory;
  BaselineEntry entry = nullptr;
};

constexpr std::size_t kInvalidInstructionIndex = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kInvalidNativeOffset = std::numeric_limits<std::size_t>::max();

}  // namespace minijs
