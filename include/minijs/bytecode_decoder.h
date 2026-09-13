#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "minijs/chunk.h"

namespace minijs {

// Decoder 输出的结构化指令视图，供 disassembler、verifier 和 Baseline JIT 复用。
struct DecodedInstruction {
  Opcode opcode;
  std::size_t offset = 0;
  std::size_t nextOffset = 0;

  // 当前指令最多 3 个固定操作数；Closure 的变长 upvalue 元数据由 nextOffset 覆盖。
  std::array<std::uint32_t, 3> operands{};
  std::uint8_t operandCount = 0;

  bool hasJumpTarget = false;
  std::size_t jumpTarget = 0;
};

const char* opcodeName(Opcode opcode);

// 从 offset 开始解析一条指令；格式错误时抛 RuntimeError。
DecodedInstruction decodeInstruction(const Chunk& chunk, std::size_t offset);

struct BytecodeVerificationResult {
  bool valid = false;
  std::string error;
  std::vector<std::size_t> instructionOffsets;
};

// 轻量结构检查：指令边界、常量索引、feedback slot 类型和跳转目标。
BytecodeVerificationResult verifyBytecode(const Chunk& chunk);

}  // namespace minijs
