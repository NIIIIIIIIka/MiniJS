#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "minijs/value.h"

namespace minijs {

enum class Opcode : std::uint8_t {
  Constant,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Negate,
  Return,
};

class Chunk {
 public:
  void writeOpcode(Opcode opcode);
  void writeByte(std::uint8_t byte);

  std::uint8_t readByte(std::size_t offset) const;

  std::size_t addConstant(Value value);
  const Value& constant(std::size_t index) const;

  const std::vector<std::uint8_t>& code() const;
  const std::vector<Value>& constants() const;

 private:
  std::vector<std::uint8_t> code_;
  std::vector<Value> constants_;
};

}  // namespace minijs
