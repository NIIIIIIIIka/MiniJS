#include "minijs/baseline_compiler.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "minijs/baseline_runtime.h"
#include "minijs/bytecode_decoder.h"

namespace minijs {
namespace {

struct BranchPatch {
  std::size_t offset = 0;
  bool conditional = false;
};

// 极小 ARM64 emitter：只负责写入当前 BaselineCompiler 需要的固定指令。
// 这里有意不抽象成通用 assembler，避免第一版 backend 扩散出过早接口。
class Arm64Emitter {
 public:
  std::size_t offset() const { return code_.size(); }
  const std::vector<std::uint8_t>& bytes() const { return code_; }

  // x0 初始保存 BaselineFrame*。helper 调用会使用 x0/x1，所以把 frame
  // 固定保存在 callee-saved x19，直到 epilogue 统一恢复。
  void emitPrologue() {
    emit32(0xA9BF7BFD);  // stp x29, x30, [sp, #-16]!
    emit32(0x910003FD);  // mov x29, sp
    emit32(0xA9BF53F3);  // stp x19, x20, [sp, #-16]!
    emit32(0xAA0003F3);  // mov x19, x0
  }

  // 所有 helper 失败路径和 Return 成功路径都会跳到同一个 epilogue，
  // 这样机器码出口只需要维护一份寄存器恢复逻辑。
  void emitEpilogue() {
    emit32(0xA8C153F3);  // ldp x19, x20, [sp], #16
    emit32(0xA8C17BFD);  // ldp x29, x30, [sp], #16
    emit32(0xD65F03C0);  // ret
  }

  void emitMovFrameToX0() {
    emit32(0xAA1303E0);  // mov x0, x19
  }

  void emitMovW1Imm32(std::uint32_t value) {
    emitMovzW(1, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(1, static_cast<std::uint16_t>(value >> 16), 1);
    }
  }

  void emitLoadX16Imm64(std::uintptr_t value) {
    emitMovzX(16, static_cast<std::uint16_t>(value & 0xffff), 0);
    for (std::uint32_t shift = 1; shift < 4; ++shift) {
      const std::uint16_t part = static_cast<std::uint16_t>((value >> (shift * 16)) & 0xffff);
      if (part != 0) {
        emitMovkX(16, part, shift);
      }
    }
  }

  void emitBlrX16() {
    emit32(0xD63F0200);  // blr x16
  }

  std::size_t emitCbzW0Placeholder() {
    const std::size_t patchOffset = offset();
    emit32(0x34000000);  // cbz w0, <patch>
    return patchOffset;
  }

  std::size_t emitBPlaceholder() {
    const std::size_t patchOffset = offset();
    emit32(0x14000000);  // b <patch>
    return patchOffset;
  }

  // ARM64 分支立即数以当前指令地址为基准，单位是 4 字节指令。
  // emitter 先写 placeholder，等 epilogue offset 确定后统一回填。
  bool patchBranchTo(std::size_t branchOffset, std::size_t targetOffset, bool conditional,
                     std::string& error) {
    if (branchOffset + sizeof(std::uint32_t) > code_.size() ||
        branchOffset % sizeof(std::uint32_t) != 0 || targetOffset % sizeof(std::uint32_t) != 0) {
      error = "unaligned ARM64 branch patch";
      return false;
    }

    const std::int64_t displacement =
        static_cast<std::int64_t>(targetOffset) - static_cast<std::int64_t>(branchOffset);
    if (displacement % 4 != 0) {
      error = "unaligned ARM64 branch target";
      return false;
    }

    const std::int64_t immediate = displacement / 4;
    std::uint32_t instruction = 0;
    if (conditional) {
      if (immediate < -(1 << 18) || immediate >= (1 << 18)) {
        error = "ARM64 conditional branch target out of range";
        return false;
      }
      instruction = 0x34000000 | ((static_cast<std::uint32_t>(immediate) & 0x7ffff) << 5);
    } else {
      if (immediate < -(1 << 25) || immediate >= (1 << 25)) {
        error = "ARM64 branch target out of range";
        return false;
      }
      instruction = 0x14000000 | (static_cast<std::uint32_t>(immediate) & 0x03ffffff);
    }

    write32(branchOffset, instruction);
    return true;
  }

 private:
  void emit32(std::uint32_t instruction) {
    code_.push_back(static_cast<std::uint8_t>(instruction & 0xff));
    code_.push_back(static_cast<std::uint8_t>((instruction >> 8) & 0xff));
    code_.push_back(static_cast<std::uint8_t>((instruction >> 16) & 0xff));
    code_.push_back(static_cast<std::uint8_t>((instruction >> 24) & 0xff));
  }

  void write32(std::size_t patchOffset, std::uint32_t instruction) {
    code_[patchOffset] = static_cast<std::uint8_t>(instruction & 0xff);
    code_[patchOffset + 1] = static_cast<std::uint8_t>((instruction >> 8) & 0xff);
    code_[patchOffset + 2] = static_cast<std::uint8_t>((instruction >> 16) & 0xff);
    code_[patchOffset + 3] = static_cast<std::uint8_t>((instruction >> 24) & 0xff);
  }

  void emitMovzW(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0x52800000 | ((shift & 0x1) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  void emitMovkW(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0x72800000 | ((shift & 0x1) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  void emitMovzX(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0xD2800000 | ((shift & 0x3) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  void emitMovkX(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0xF2800000 | ((shift & 0x3) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  std::vector<std::uint8_t> code_;
};

BaselineCompileResult failure(std::string error) {
  BaselineCompileResult result;
  result.error = std::move(error);
  return result;
}

// Runtime helper 统一返回 bool 到 w0。调用者随后发 cbz w0, epilogue，
// 让 helper 内部捕获到的异常通过 frame->failed 传播回 C++ 边界。
void emitRuntimeCall(Arm64Emitter& emitter, std::uintptr_t functionAddress) {
  emitter.emitMovFrameToX0();
  emitter.emitLoadX16Imm64(functionAddress);
  emitter.emitBlrX16();
}

}  // namespace

BaselineCompileResult BaselineCompiler::compile(const BytecodeFunction& function) const {
  // native compiler 仍以 verifier + decoder 为前端，确保 codegen 只处理
  // 已确认边界和操作数合法的现有 bytecode Opcode。
  const BytecodeVerificationResult verification = verifyBytecode(function.chunk);
  if (!verification.valid) {
    return failure("bytecode verification failed: " + verification.error);
  }

  auto code = std::make_shared<BaselineCode>();
  code->bytecodeOffsetToInstructionIndex.assign(function.chunk.code().size() + 1,
                                                kInvalidInstructionIndex);
  code->bytecodeOffsetToNativeOffset.assign(function.chunk.code().size() + 1,
                                            kInvalidNativeOffset);
  code->instructions.reserve(verification.instructionOffsets.size());

  for (std::size_t index = 0; index < verification.instructionOffsets.size(); ++index) {
    const std::size_t bytecodeOffset = verification.instructionOffsets[index];
    code->bytecodeOffsetToInstructionIndex[bytecodeOffset] = index;
  }

  Arm64Emitter emitter;
  std::vector<BranchPatch> epiloguePatches;
  emitter.emitPrologue();

  // bytecodeOffsetToNativeOffset 记录每条 bytecode 指令对应的机器码起点；
  // 以后做调试、safepoint 或 deopt 时会从这里回到 bytecode 语义位置。
  for (const std::size_t bytecodeOffset : verification.instructionOffsets) {
    DecodedInstruction instruction = decodeInstruction(function.chunk, bytecodeOffset);
    code->bytecodeOffsetToNativeOffset[bytecodeOffset] = emitter.offset();

    switch (instruction.opcode) {
      case Opcode::Constant:
        emitter.emitMovFrameToX0();
        emitter.emitMovW1Imm32(instruction.operands[0]);
        emitter.emitLoadX16Imm64(reinterpret_cast<std::uintptr_t>(&minijsBaselinePushConstant));
        emitter.emitBlrX16();
        epiloguePatches.push_back({emitter.emitCbzW0Placeholder(), true});
        break;

      case Opcode::GetLocal:
        emitter.emitMovFrameToX0();
        emitter.emitMovW1Imm32(instruction.operands[0]);
        emitter.emitLoadX16Imm64(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
        emitter.emitBlrX16();
        epiloguePatches.push_back({emitter.emitCbzW0Placeholder(), true});
        break;

      case Opcode::Add:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd));
        epiloguePatches.push_back({emitter.emitCbzW0Placeholder(), true});
        break;

      case Opcode::Return:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineReturn));
        epiloguePatches.push_back({emitter.emitCbzW0Placeholder(), true});
        epiloguePatches.push_back({emitter.emitBPlaceholder(), false});
        break;

      default:
        return failure(std::string("baseline compiler does not support ") +
                       opcodeName(instruction.opcode));
    }

    code->instructions.push_back(instruction);
  }

  const std::size_t epilogueOffset = emitter.offset();
  code->bytecodeOffsetToNativeOffset[function.chunk.code().size()] = epilogueOffset;
  emitter.emitEpilogue();

  for (const BranchPatch& patch : epiloguePatches) {
    std::string patchError;
    if (!emitter.patchBranchTo(patch.offset, epilogueOffset, patch.conditional, patchError)) {
      return failure(patchError);
    }
  }

  std::string memoryError;
  code->executableMemory = ExecutableMemory::allocate(emitter.bytes(), memoryError);
  if (code->executableMemory == nullptr) {
    return failure("executable memory allocation failed: " + memoryError);
  }
  code->entry = reinterpret_cast<BaselineEntry>(code->executableMemory->data());

  BaselineCompileResult result;
  result.code = std::move(code);
  return result;
}

}  // namespace minijs
