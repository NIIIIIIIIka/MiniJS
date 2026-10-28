#pragma once

#include <cstdint>
#include <vector>

namespace minijs::detail {

enum class X64Register : std::uint8_t {
  Rcx = 1,
  Rdx = 2,
};

enum class X64XmmRegister : std::uint8_t {
  Xmm0 = 0,
  Xmm1 = 1,
};

struct X64Operand {
  X64Register base;
  std::uint32_t displacement;
};

// 只负责把 x64 指令编码到缓冲区；不理解 MiniJS Value、栈槽或 bytecode。
class X64Assembler {
 public:
  explicit X64Assembler(std::vector<std::uint8_t>& code) : code_(code) {}

  void movsd(X64XmmRegister destination, X64Operand source) {
    emitScalarDoubleMemoryInstruction(0x10, destination, source);
  }

  void movsd(X64Operand destination, X64XmmRegister source) {
    emitScalarDoubleMemoryInstruction(0x11, source, destination);
  }

  void addsd(X64XmmRegister destination, X64Operand source) {
    emitScalarDoubleMemoryInstruction(0x58, destination, source);
  }

  void addsd(X64XmmRegister destination, X64XmmRegister source) {
    emitScalarDoubleRegisterInstruction(0x58, destination, source);
  }

  void subsd(X64XmmRegister destination, X64Operand source) {
    emitScalarDoubleMemoryInstruction(0x5C, destination, source);
  }

  void subsd(X64XmmRegister destination, X64XmmRegister source) {
    emitScalarDoubleRegisterInstruction(0x5C, destination, source);
  }

  void mulsd(X64XmmRegister destination, X64Operand source) {
    emitScalarDoubleMemoryInstruction(0x59, destination, source);
  }

  void mulsd(X64XmmRegister destination, X64XmmRegister source) {
    emitScalarDoubleRegisterInstruction(0x59, destination, source);
  }

  void divsd(X64XmmRegister destination, X64Operand source) {
    emitScalarDoubleMemoryInstruction(0x5E, destination, source);
  }

  void divsd(X64XmmRegister destination, X64XmmRegister source) {
    emitScalarDoubleRegisterInstruction(0x5E, destination, source);
  }

 private:
  void emitScalarDoubleMemoryInstruction(std::uint8_t opcode, X64XmmRegister reg,
                                         X64Operand operand) {
    code_.push_back(0xF2);
    code_.push_back(0x0F);
    code_.push_back(opcode);
    // mod=10 表示 base + disp32；reg 是 XMM 编号，r/m 是通用寄存器编号。
    code_.push_back(static_cast<std::uint8_t>(0x80 | (static_cast<std::uint8_t>(reg) << 3) |
                                              static_cast<std::uint8_t>(operand.base)));
    emit32(operand.displacement);
  }

  void emitScalarDoubleRegisterInstruction(std::uint8_t opcode, X64XmmRegister destination,
                                           X64XmmRegister source) {
    code_.push_back(0xF2);
    code_.push_back(0x0F);
    code_.push_back(opcode);
    code_.push_back(static_cast<std::uint8_t>(0xC0 | (static_cast<std::uint8_t>(destination) << 3) |
                                              static_cast<std::uint8_t>(source)));
  }

  void emit32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      code_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
  }

  std::vector<std::uint8_t>& code_;
};

}  // namespace minijs::detail
