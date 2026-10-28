#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "../baseline_codegen_types.h"

namespace minijs::detail {

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

  //把 X19 寄存器的值复制到 X0 寄存器
  void emitMovFrameToX0() {
    emit32(0xAA1303E0);  // mov x0, x19
  }

  void emitMoveFrameToFirstArg() { emitMovFrameToX0(); }

  void emitMoveU32ToSecondArg(std::uint32_t value) { emitMovW1Imm32(value); }

  void emitMoveU32ToThirdArg(std::uint32_t value) { emitMovW2Imm32(value); }

  void emitMoveU32ToFourthArg(std::uint32_t value) { emitMovW3Imm32(value); }

  void emitLoadHelper(std::uintptr_t value) { emitLoadX16Imm64(value); }

  void emitCallHelper() {
    emitBlrX16();
    // Runtime ABI 只返回 bool/uint8_t；分支检查 w0 前先清除窄返回值的高位。
    emit32(0x53001C00);  // uxtb w0, w0
  }

  std::size_t emitJumpIfFalsePlaceholder() { return emitCbzW0Placeholder(); }

  std::size_t emitJumpPlaceholder() { return emitBPlaceholder(); }

  std::size_t emitJumpIfZeroPlaceholder() { return emitCbzW0Placeholder(); }

  std::size_t emitJumpIfNotOnePlaceholder() {
    emit32(0x7100041F);  // cmp w0, #1
    const std::size_t patchOffset = offset();
    emit32(0x54000001);  // b.ne <patch>
    return patchOffset;
  }

  void emitMovW1Imm32(std::uint32_t value) {
    emitMovzW(1, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(1, static_cast<std::uint16_t>(value >> 16), 1);
    }
  }

  void emitMovW2Imm32(std::uint32_t value) {
    emitMovzW(2, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(2, static_cast<std::uint16_t>(value >> 16), 1);
    }
  }

  void emitMovW3Imm32(std::uint32_t value) {
    emitMovzW(3, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(3, static_cast<std::uint16_t>(value >> 16), 1);
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
    return patchBranchTo(branchOffset,
                         targetOffset,
                         conditional ? NativeBranchKind::Zero
                                     : NativeBranchKind::Unconditional,
                         error);
  }

  bool patchBranchTo(std::size_t branchOffset, std::size_t targetOffset,
                     NativeBranchKind kind, std::string& error) {
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
    if (kind == NativeBranchKind::Zero) {
      if (immediate < -(1 << 18) || immediate >= (1 << 18)) {
        error = "ARM64 conditional branch target out of range";
        return false;
      }
      instruction = 0x34000000 | ((static_cast<std::uint32_t>(immediate) & 0x7ffff) << 5);
    } else if (kind == NativeBranchKind::NotOne) {
      if (immediate < -(1 << 18) || immediate >= (1 << 18)) {
        error = "ARM64 conditional branch target out of range";
        return false;
      }
      instruction = 0x54000001 | ((static_cast<std::uint32_t>(immediate) & 0x7ffff) << 5);
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
    //Movz / Movk   是 movz 还是 movk 指令
    //W / X    宽度 W=32 位,X=64 位
    //发射一条 64 位的 movz 指令
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

}  // namespace minijs::detail
