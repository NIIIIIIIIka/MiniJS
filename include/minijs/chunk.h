#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "minijs/value.h"

namespace minijs {

// VM 支持的字节码指令。
enum class Opcode : std::uint8_t {
  Constant,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Negate,
  Return,
  DefineGlobal,
  GetGlobal,
  SetGlobal,
  GetLocal,
  SetLocal,
  Pop,
  Equal,
  Greater,
  Less,
  Not,
  JumpIfFalse,
  Jump,
  Loop,
  Call,
  Array,
  GetIndex,
  SetIndex,
};

// 一段可执行字节码，包含指令流和常量池。
class Chunk {
 public:
  void writeOpcode(Opcode opcode);
  void writeByte(std::uint8_t byte);

  std::uint8_t readByte(std::size_t offset) const;

  std::size_t addConstant(Value value);
  const Value& constant(std::size_t index) const;

  const std::vector<std::uint8_t>& code() const;
  const std::vector<Value>& constants() const;

  // 当前指令流长度，用于编译跳转目标。
  std::size_t count() const;
  // 回填已写入的占位字节，例如跳转偏移。
  void patchByte(std::size_t offset, std::uint8_t byte);

 private:
  std::vector<std::uint8_t> code_;
  std::vector<Value> constants_;
};

}  // namespace minijs
