#include "minijs/disassembler.h"

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

namespace minijs {
namespace {

void writeOffset(std::size_t offset, std::ostream& output) {
  output << std::setw(4) << std::setfill('0') << offset << " ";
  output << std::setfill(' ');
}

std::size_t simpleInstruction(std::string_view name, std::size_t offset, std::ostream& output) {
  output << name << '\n';
  return offset + 1;
}

std::size_t constantInstruction(const Chunk& chunk, std::string_view name, std::size_t offset,
                                std::ostream& output) {
  const std::uint8_t index = chunk.readByte(offset + 1);
  output << name << " " << static_cast<int>(index) << " " << chunk.constant(index).toString()
         << '\n';
  return offset + 2;
}

}  // namespace

std::string disassembleChunk(const Chunk& chunk) {
  std::ostringstream output;

  std::size_t offset = 0;
  while (offset < chunk.code().size()) {
    offset = disassembleInstruction(chunk, offset, output);
  }

  return output.str();
}

std::size_t disassembleInstruction(const Chunk& chunk, std::size_t offset, std::ostream& output) {
  writeOffset(offset, output);

  const Opcode opcode = static_cast<Opcode>(chunk.readByte(offset));
  switch (opcode) {
    case Opcode::Constant:
      return constantInstruction(chunk, "OP_CONSTANT", offset, output);
    case Opcode::Add:
      return simpleInstruction("OP_ADD", offset, output);
    case Opcode::Sub:
      return simpleInstruction("OP_SUB", offset, output);
    case Opcode::Mul:
      return simpleInstruction("OP_MUL", offset, output);
    case Opcode::Div:
      return simpleInstruction("OP_DIV", offset, output);
    case Opcode::Mod:
      return simpleInstruction("OP_MOD", offset, output);
    case Opcode::Negate:
      return simpleInstruction("OP_NEGATE", offset, output);
    case Opcode::Return:
      return simpleInstruction("OP_RETURN", offset, output);
  }

  output << "OP_UNKNOWN " << static_cast<int>(chunk.readByte(offset)) << '\n';
  return offset + 1;
}

}  // namespace minijs
