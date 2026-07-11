#include "minijs/disassembler.h"

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

#include "minijs/bytecode_function.h"

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

std::size_t nameInstruction(const Chunk& chunk, std::string_view name, std::size_t offset,
                            std::ostream& output) {
  return constantInstruction(chunk, name, offset, output);
}

std::size_t byteInstruction(const Chunk& chunk, std::string_view name, std::size_t offset,
                            std::ostream& output) {
  const std::uint8_t slot = chunk.readByte(offset + 1);
  output << name << " " << static_cast<int>(slot) << '\n';
  return offset + 2;
}

std::size_t jumpInstruction(const Chunk& chunk, std::string_view name, std::size_t offset,
                            std::ostream& output) {
  const std::uint16_t jump =
      static_cast<std::uint16_t>((chunk.readByte(offset + 1) << 8) | chunk.readByte(offset + 2));
  output << name << " " << offset << " -> " << offset + 3 + jump << '\n';
  return offset + 3;
}

std::size_t loopInstruction(const Chunk& chunk, std::string_view name, std::size_t offset,
                            std::ostream& output) {
  const std::uint16_t jump =
      static_cast<std::uint16_t>((chunk.readByte(offset + 1) << 8) | chunk.readByte(offset + 2));
  output << name << " " << offset << " -> " << offset + 3 - jump << '\n';
  return offset + 3;
}

std::size_t methodCallInstruction(const Chunk& chunk, std::string_view name, std::size_t offset,
                                  std::ostream& output) {
  const std::uint8_t nameIndex = chunk.readByte(offset + 1);
  const std::uint8_t argCount = chunk.readByte(offset + 2);
  output << name << " " << static_cast<int>(nameIndex) << " "
         << chunk.constant(nameIndex).toString() << " " << static_cast<int>(argCount) << '\n';
  return offset + 3;
}

std::size_t closureInstruction(const Chunk& chunk, std::size_t offset, std::ostream& output) {
  const std::uint8_t index = chunk.readByte(offset + 1);
  const auto& function = chunk.constant(index).asBytecodeFunction();
  output << "OP_CLOSURE " << static_cast<int>(index) << " "
         << chunk.constant(index).toString() << '\n';

  std::size_t next = offset + 2;
  for (const UpvalueDescriptor& upvalue : function->upvalues) {
    const std::uint8_t isLocal = chunk.readByte(next++);
    const std::uint8_t slot = chunk.readByte(next++);
    output << "     | " << (isLocal != 0 ? "local" : "upvalue") << " "
           << static_cast<int>(slot) << '\n';
    (void)upvalue;
  }

  return next;
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
    case Opcode::DefineGlobal:
      return nameInstruction(chunk, "OP_DEFINE_GLOBAL", offset, output);
    case Opcode::GetGlobal:
      return nameInstruction(chunk, "OP_GET_GLOBAL", offset, output);
    case Opcode::SetGlobal:
      return nameInstruction(chunk, "OP_SET_GLOBAL", offset, output);
    case Opcode::GetLocal:
      return byteInstruction(chunk, "OP_GET_LOCAL", offset, output);
    case Opcode::SetLocal:
      return byteInstruction(chunk, "OP_SET_LOCAL", offset, output);
    case Opcode::Pop:
      return simpleInstruction("OP_POP", offset, output);
    case Opcode::Not:
      return simpleInstruction("OP_NOT", offset, output);
    case Opcode::Equal:
      return simpleInstruction("OP_EQUAL", offset, output);
    case Opcode::Greater:
      return simpleInstruction("OP_GREATER", offset, output);
    case Opcode::Less:
      return simpleInstruction("OP_LESS", offset, output);
    case Opcode::JumpIfFalse:
      return jumpInstruction(chunk, "OP_JUMP_IF_FALSE", offset, output);
    case Opcode::Jump:
      return jumpInstruction(chunk, "OP_JUMP", offset, output);
    case Opcode::Loop:
      return loopInstruction(chunk, "OP_LOOP", offset, output);
    case Opcode::Call:
      return byteInstruction(chunk, "OP_CALL", offset, output);
    case Opcode::Array:
      return byteInstruction(chunk, "OP_ARRAY", offset, output);
    case Opcode::GetIndex:
      return simpleInstruction("OP_GET_INDEX", offset, output);
    case Opcode::SetIndex:
      return simpleInstruction("OP_SET_INDEX", offset, output);
    case Opcode::Object:
      return constantInstruction(chunk, "OP_OBJECT", offset, output);
    case Opcode::GetProperty:
      return constantInstruction(chunk, "OP_GET_PROPERTY", offset, output);
    case Opcode::SetProperty:
      return constantInstruction(chunk, "OP_SET_PROPERTY", offset, output);
    case Opcode::MethodCall:
      return methodCallInstruction(chunk, "OP_METHOD_CALL", offset, output);
    case Opcode::Closure:
      return closureInstruction(chunk, offset, output);
    case Opcode::GetUpvalue:
      return byteInstruction(chunk, "OP_GET_UPVALUE", offset, output);
    case Opcode::SetUpvalue:
      return byteInstruction(chunk, "OP_SET_UPVALUE", offset, output);
    case Opcode::CloseUpvalue:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset, output);
    case Opcode::GetCurrentClosure:
      return simpleInstruction("OP_GET_CURRENT_CLOSURE", offset, output);
    case Opcode::Class:
      return constantInstruction(chunk, "OP_CLASS", offset, output);
    case Opcode::Method:
      return constantInstruction(chunk, "OP_METHOD", offset, output);
  }

  output << "OP_UNKNOWN " << static_cast<int>(chunk.readByte(offset)) << '\n';
  return offset + 1;
}

}  // namespace minijs
