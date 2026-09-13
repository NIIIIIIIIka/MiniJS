#include "minijs/disassembler.h"

#include <cstdint>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

#include "minijs/bytecode_decoder.h"
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

std::size_t constantInstruction(const Chunk& chunk, const DecodedInstruction& instruction,
                                std::string_view name,
                                std::ostream& output) {
  const std::uint8_t index = static_cast<std::uint8_t>(instruction.operands[0]);
  output << name << " " << static_cast<int>(index) << " " << chunk.constant(index).toString()
         << '\n';
  return instruction.nextOffset;
}

std::size_t nameInstruction(const Chunk& chunk, const DecodedInstruction& instruction,
                            std::string_view name,
                            std::ostream& output) {
  return constantInstruction(chunk, instruction, name, output);
}

std::size_t byteInstruction(const DecodedInstruction& instruction, std::string_view name,
                            std::ostream& output) {
  const std::uint8_t slot = static_cast<std::uint8_t>(instruction.operands[0]);
  output << name << " " << static_cast<int>(slot) << '\n';
  return instruction.nextOffset;
}

std::size_t propertyInstruction(const Chunk& chunk, const DecodedInstruction& instruction,
                                std::string_view name,
                                std::ostream& output) {
  const std::uint8_t nameIndex = static_cast<std::uint8_t>(instruction.operands[0]);
  const std::uint8_t feedbackSlot = static_cast<std::uint8_t>(instruction.operands[1]);
  output << name << " " << static_cast<int>(nameIndex) << " "
         << chunk.constant(nameIndex).toString() << " feedback="
         << static_cast<int>(feedbackSlot) << '\n';
  return instruction.nextOffset;
}

std::size_t jumpInstruction(const DecodedInstruction& instruction, std::string_view name,
                            std::ostream& output) {
  output << name << " " << instruction.offset << " -> " << instruction.jumpTarget << '\n';
  return instruction.nextOffset;
}

std::size_t methodCallInstruction(const Chunk& chunk, const DecodedInstruction& instruction,
                                  std::string_view name,
                                  std::ostream& output) {
  const std::uint8_t nameIndex = static_cast<std::uint8_t>(instruction.operands[0]);
  const std::uint8_t argCount = static_cast<std::uint8_t>(instruction.operands[1]);
  const std::uint8_t feedbackSlot = static_cast<std::uint8_t>(instruction.operands[2]);
  output << name << " " << static_cast<int>(nameIndex) << " "
         << chunk.constant(nameIndex).toString() << " " << static_cast<int>(argCount)
         << " feedback=" << static_cast<int>(feedbackSlot) << '\n';
  return instruction.nextOffset;
}

std::size_t superCallInstruction(const Chunk& chunk, const DecodedInstruction& instruction,
                                 std::string_view name,
                                 std::ostream& output) {
  const std::uint8_t nameIndex = static_cast<std::uint8_t>(instruction.operands[0]);
  const std::uint8_t argCount = static_cast<std::uint8_t>(instruction.operands[1]);
  output << name << " " << static_cast<int>(nameIndex) << " "
         << chunk.constant(nameIndex).toString() << " " << static_cast<int>(argCount) << '\n';
  return instruction.nextOffset;
}

std::size_t closureInstruction(const Chunk& chunk, const DecodedInstruction& instruction,
                               std::ostream& output) {
  const std::uint8_t index = static_cast<std::uint8_t>(instruction.operands[0]);
  const auto& function = chunk.constant(index).asBytecodeFunction();
  output << "OP_CLOSURE " << static_cast<int>(index) << " "
         << chunk.constant(index).toString() << '\n';

  std::size_t next = instruction.offset + 2;
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

  // 反汇编只负责展示；指令长度、操作数和跳转目标统一由 Decoder 解析。
  const DecodedInstruction instruction = decodeInstruction(chunk, offset);
  switch (instruction.opcode) {
    case Opcode::Constant:
      return constantInstruction(chunk, instruction, "OP_CONSTANT", output);
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
      return nameInstruction(chunk, instruction, "OP_DEFINE_GLOBAL", output);
    case Opcode::GetGlobal:
      return nameInstruction(chunk, instruction, "OP_GET_GLOBAL", output);
    case Opcode::SetGlobal:
      return nameInstruction(chunk, instruction, "OP_SET_GLOBAL", output);
    case Opcode::GetLocal:
      return byteInstruction(instruction, "OP_GET_LOCAL", output);
    case Opcode::SetLocal:
      return byteInstruction(instruction, "OP_SET_LOCAL", output);
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
      return jumpInstruction(instruction, "OP_JUMP_IF_FALSE", output);
    case Opcode::Jump:
      return jumpInstruction(instruction, "OP_JUMP", output);
    case Opcode::Loop:
      return jumpInstruction(instruction, "OP_LOOP", output);
    case Opcode::Call:
      return byteInstruction(instruction, "OP_CALL", output);
    case Opcode::Array:
      return byteInstruction(instruction, "OP_ARRAY", output);
    case Opcode::GetIndex:
      return simpleInstruction("OP_GET_INDEX", offset, output);
    case Opcode::SetIndex:
      return simpleInstruction("OP_SET_INDEX", offset, output);
    case Opcode::Object:
      return constantInstruction(chunk, instruction, "OP_OBJECT", output);
    case Opcode::GetProperty:
      return propertyInstruction(chunk, instruction, "OP_GET_PROPERTY", output);
    case Opcode::SetProperty:
      return propertyInstruction(chunk, instruction, "OP_SET_PROPERTY", output);
    case Opcode::MethodCall:
      return methodCallInstruction(chunk, instruction, "OP_METHOD_CALL", output);
    case Opcode::Closure:
      return closureInstruction(chunk, instruction, output);
    case Opcode::GetUpvalue:
      return byteInstruction(instruction, "OP_GET_UPVALUE", output);
    case Opcode::SetUpvalue:
      return byteInstruction(instruction, "OP_SET_UPVALUE", output);
    case Opcode::CloseUpvalue:
      return simpleInstruction("OP_CLOSE_UPVALUE", offset, output);
    case Opcode::GetCurrentClosure:
      return simpleInstruction("OP_GET_CURRENT_CLOSURE", offset, output);
    case Opcode::Class:
      return constantInstruction(chunk, instruction, "OP_CLASS", output);
    case Opcode::Method:
      return constantInstruction(chunk, instruction, "OP_METHOD", output);
    case Opcode::StaticMethod:
      return constantInstruction(chunk, instruction, "OP_STATIC_METHOD", output);
    case Opcode::Inherit:
      return simpleInstruction("OP_INHERIT", offset, output);
    case Opcode::SuperCall:
      return superCallInstruction(chunk, instruction, "OP_SUPER_CALL", output);
  }

  output << "OP_UNKNOWN " << static_cast<int>(chunk.readByte(offset)) << '\n';
  return offset + 1;
}

}  // namespace minijs
