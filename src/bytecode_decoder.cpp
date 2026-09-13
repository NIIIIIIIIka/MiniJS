#include "minijs/bytecode_decoder.h"

#include <algorithm>
#include <cstdint>
#include <string>

#include "minijs/bytecode_function.h"
#include "minijs/runtime_error.h"

namespace minijs {
namespace {

std::uint16_t readShort(const Chunk& chunk, std::size_t offset) {
  const std::uint16_t high = chunk.readByte(offset);
  const std::uint16_t low = chunk.readByte(offset + 1);
  return static_cast<std::uint16_t>((high << 8) | low);
}

DecodedInstruction makeInstruction(Opcode opcode, std::size_t offset, std::size_t length) {
  DecodedInstruction instruction;
  instruction.opcode = opcode;
  instruction.offset = offset;
  instruction.nextOffset = offset + length;
  return instruction;
}

DecodedInstruction makeSimple(Opcode opcode, std::size_t offset) {
  return makeInstruction(opcode, offset, 1);
}

DecodedInstruction makeByteOperand(const Chunk& chunk, Opcode opcode, std::size_t offset) {
  DecodedInstruction instruction = makeInstruction(opcode, offset, 2);
  instruction.operands[0] = chunk.readByte(offset + 1);
  instruction.operandCount = 1;
  return instruction;
}

DecodedInstruction makePropertyOperand(const Chunk& chunk, Opcode opcode, std::size_t offset) {
  DecodedInstruction instruction = makeInstruction(opcode, offset, 3);
  instruction.operands[0] = chunk.readByte(offset + 1);
  instruction.operands[1] = chunk.readByte(offset + 2);
  instruction.operandCount = 2;
  return instruction;
}

DecodedInstruction makeMethodCallOperand(const Chunk& chunk, Opcode opcode, std::size_t offset) {
  DecodedInstruction instruction = makeInstruction(opcode, offset, 4);
  instruction.operands[0] = chunk.readByte(offset + 1);
  instruction.operands[1] = chunk.readByte(offset + 2);
  instruction.operands[2] = chunk.readByte(offset + 3);
  instruction.operandCount = 3;
  return instruction;
}

DecodedInstruction makeSuperCallOperand(const Chunk& chunk, Opcode opcode, std::size_t offset) {
  DecodedInstruction instruction = makeInstruction(opcode, offset, 3);
  instruction.operands[0] = chunk.readByte(offset + 1);
  instruction.operands[1] = chunk.readByte(offset + 2);
  instruction.operandCount = 2;
  return instruction;
}

DecodedInstruction makeForwardJump(const Chunk& chunk, Opcode opcode, std::size_t offset) {
  DecodedInstruction instruction = makeInstruction(opcode, offset, 3);
  const std::uint16_t jump = readShort(chunk, offset + 1);
  instruction.operands[0] = jump;
  instruction.operandCount = 1;
  instruction.hasJumpTarget = true;
  instruction.jumpTarget = instruction.nextOffset + jump;
  return instruction;
}

DecodedInstruction makeLoopJump(const Chunk& chunk, Opcode opcode, std::size_t offset) {
  DecodedInstruction instruction = makeInstruction(opcode, offset, 3);
  const std::uint16_t jump = readShort(chunk, offset + 1);
  instruction.operands[0] = jump;
  instruction.operandCount = 1;
  instruction.hasJumpTarget = true;
  if (jump > instruction.nextOffset) {
    throw RuntimeError("loop jump target before beginning of bytecode");
  }
  instruction.jumpTarget = instruction.nextOffset - jump;
  return instruction;
}

DecodedInstruction makeClosureOperand(const Chunk& chunk, Opcode opcode, std::size_t offset) {
  DecodedInstruction instruction = makeByteOperand(chunk, opcode, offset);
  const std::uint8_t functionIndex = static_cast<std::uint8_t>(instruction.operands[0]);
  const Value& value = chunk.constant(functionIndex);
  if (!value.isBytecodeFunction()) {
    throw RuntimeError("closure constant is not a bytecode function");
  }

  // OP_CLOSURE 尾部携带变长 upvalue 元数据，属于同一条指令。
  const auto& function = value.asBytecodeFunction();
  instruction.nextOffset = offset + 2 + function->upvalues.size() * 2;
  if (instruction.nextOffset > chunk.code().size()) {
    throw RuntimeError("closure upvalue metadata truncated");
  }
  return instruction;
}

bool constantOperandIsInBounds(const Chunk& chunk, const DecodedInstruction& instruction,
                               std::uint8_t operandIndex, std::string& error) {
  const std::size_t constantIndex = instruction.operands[operandIndex];
  if (constantIndex >= chunk.constants().size()) {
    error = std::string(opcodeName(instruction.opcode)) + " constant index out of bounds";
    return false;
  }
  return true;
}

bool stringConstantOperandIsValid(const Chunk& chunk, const DecodedInstruction& instruction,
                                  std::uint8_t operandIndex, std::string& error) {
  if (!constantOperandIsInBounds(chunk, instruction, operandIndex, error)) {
    return false;
  }

  if (!chunk.constant(instruction.operands[operandIndex]).isString()) {
    error = std::string(opcodeName(instruction.opcode)) + " name constant is not a string";
    return false;
  }
  return true;
}

bool objectNamesConstantIsValid(const Chunk& chunk, const DecodedInstruction& instruction,
                                std::string& error) {
  if (!constantOperandIsInBounds(chunk, instruction, 0, error)) {
    return false;
  }

  const Value& names = chunk.constant(instruction.operands[0]);
  if (!names.isArray()) {
    error = "OP_OBJECT names constant is not an array";
    return false;
  }

  for (const Value& name : names.asArray()) {
    if (!name.isString()) {
      error = "OP_OBJECT names array contains a non-string value";
      return false;
    }
  }
  return true;
}

bool feedbackSlotIsValid(const Chunk& chunk, const DecodedInstruction& instruction,
                         std::uint8_t operandIndex, FeedbackKind expectedKind,
                         std::string& error) {
  const std::size_t slotIndex = instruction.operands[operandIndex];
  if (slotIndex >= chunk.feedbackSlots().size()) {
    error = std::string(opcodeName(instruction.opcode)) + " feedback slot index out of bounds";
    return false;
  }

  if (chunk.feedbackSlots()[slotIndex].kind != expectedKind) {
    error = std::string(opcodeName(instruction.opcode)) + " feedback slot kind mismatch";
    return false;
  }

  return true;
}

bool jumpTargetIsInstructionBoundary(const std::vector<std::size_t>& instructionOffsets,
                                     std::size_t target) {
  return std::binary_search(instructionOffsets.begin(), instructionOffsets.end(), target);
}

bool verifyInstructionOperands(const Chunk& chunk, const DecodedInstruction& instruction,
                               std::string& error) {
  switch (instruction.opcode) {
    case Opcode::Constant:
      return constantOperandIsInBounds(chunk, instruction, 0, error);

    case Opcode::DefineGlobal:
    case Opcode::GetGlobal:
    case Opcode::SetGlobal:
    case Opcode::Class:
    case Opcode::Method:
    case Opcode::StaticMethod:
      return stringConstantOperandIsValid(chunk, instruction, 0, error);

    case Opcode::Object:
      return objectNamesConstantIsValid(chunk, instruction, error);

    case Opcode::GetProperty:
      return stringConstantOperandIsValid(chunk, instruction, 0, error) &&
             feedbackSlotIsValid(chunk, instruction, 1, FeedbackKind::GetProperty, error);

    case Opcode::SetProperty:
      return stringConstantOperandIsValid(chunk, instruction, 0, error) &&
             feedbackSlotIsValid(chunk, instruction, 1, FeedbackKind::SetProperty, error);

    case Opcode::MethodCall:
      return stringConstantOperandIsValid(chunk, instruction, 0, error) &&
             feedbackSlotIsValid(chunk, instruction, 2, FeedbackKind::MethodCall, error);

    case Opcode::SuperCall:
      return stringConstantOperandIsValid(chunk, instruction, 0, error);

    case Opcode::Closure:
      if (!constantOperandIsInBounds(chunk, instruction, 0, error)) {
        return false;
      }
      if (!chunk.constant(instruction.operands[0]).isBytecodeFunction()) {
        error = "OP_CLOSURE constant is not a bytecode function";
        return false;
      }
      return true;

    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::Div:
    case Opcode::Mod:
    case Opcode::Negate:
    case Opcode::Return:
    case Opcode::GetLocal:
    case Opcode::SetLocal:
    case Opcode::Pop:
    case Opcode::Equal:
    case Opcode::Greater:
    case Opcode::Less:
    case Opcode::Not:
    case Opcode::JumpIfFalse:
    case Opcode::Jump:
    case Opcode::Loop:
    case Opcode::Call:
    case Opcode::Array:
    case Opcode::GetIndex:
    case Opcode::SetIndex:
    case Opcode::GetUpvalue:
    case Opcode::SetUpvalue:
    case Opcode::CloseUpvalue:
    case Opcode::GetCurrentClosure:
    case Opcode::Inherit:
      return true;
  }

  error = "unknown opcode";
  return false;
}

}  // namespace

const char* opcodeName(Opcode opcode) {
  switch (opcode) {
    case Opcode::Constant:
      return "OP_CONSTANT";
    case Opcode::Add:
      return "OP_ADD";
    case Opcode::Sub:
      return "OP_SUB";
    case Opcode::Mul:
      return "OP_MUL";
    case Opcode::Div:
      return "OP_DIV";
    case Opcode::Mod:
      return "OP_MOD";
    case Opcode::Negate:
      return "OP_NEGATE";
    case Opcode::Return:
      return "OP_RETURN";
    case Opcode::DefineGlobal:
      return "OP_DEFINE_GLOBAL";
    case Opcode::GetGlobal:
      return "OP_GET_GLOBAL";
    case Opcode::SetGlobal:
      return "OP_SET_GLOBAL";
    case Opcode::GetLocal:
      return "OP_GET_LOCAL";
    case Opcode::SetLocal:
      return "OP_SET_LOCAL";
    case Opcode::Pop:
      return "OP_POP";
    case Opcode::Equal:
      return "OP_EQUAL";
    case Opcode::Greater:
      return "OP_GREATER";
    case Opcode::Less:
      return "OP_LESS";
    case Opcode::Not:
      return "OP_NOT";
    case Opcode::JumpIfFalse:
      return "OP_JUMP_IF_FALSE";
    case Opcode::Jump:
      return "OP_JUMP";
    case Opcode::Loop:
      return "OP_LOOP";
    case Opcode::Call:
      return "OP_CALL";
    case Opcode::Array:
      return "OP_ARRAY";
    case Opcode::GetIndex:
      return "OP_GET_INDEX";
    case Opcode::SetIndex:
      return "OP_SET_INDEX";
    case Opcode::Object:
      return "OP_OBJECT";
    case Opcode::GetProperty:
      return "OP_GET_PROPERTY";
    case Opcode::SetProperty:
      return "OP_SET_PROPERTY";
    case Opcode::MethodCall:
      return "OP_METHOD_CALL";
    case Opcode::Closure:
      return "OP_CLOSURE";
    case Opcode::GetUpvalue:
      return "OP_GET_UPVALUE";
    case Opcode::SetUpvalue:
      return "OP_SET_UPVALUE";
    case Opcode::CloseUpvalue:
      return "OP_CLOSE_UPVALUE";
    case Opcode::GetCurrentClosure:
      return "OP_GET_CURRENT_CLOSURE";
    case Opcode::Class:
      return "OP_CLASS";
    case Opcode::Method:
      return "OP_METHOD";
    case Opcode::StaticMethod:
      return "OP_STATIC_METHOD";
    case Opcode::Inherit:
      return "OP_INHERIT";
    case Opcode::SuperCall:
      return "OP_SUPER_CALL";
  }

  return "OP_UNKNOWN";
}

DecodedInstruction decodeInstruction(const Chunk& chunk, std::size_t offset) {
  const std::uint8_t rawOpcode = chunk.readByte(offset);
  const Opcode opcode = static_cast<Opcode>(rawOpcode);

  switch (opcode) {
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::Div:
    case Opcode::Mod:
    case Opcode::Negate:
    case Opcode::Return:
    case Opcode::Pop:
    case Opcode::Equal:
    case Opcode::Greater:
    case Opcode::Less:
    case Opcode::Not:
    case Opcode::GetIndex:
    case Opcode::SetIndex:
    case Opcode::CloseUpvalue:
    case Opcode::GetCurrentClosure:
    case Opcode::Inherit:
      return makeSimple(opcode, offset);

    case Opcode::Constant:
    case Opcode::DefineGlobal:
    case Opcode::GetGlobal:
    case Opcode::SetGlobal:
    case Opcode::GetLocal:
    case Opcode::SetLocal:
    case Opcode::Call:
    case Opcode::Array:
    case Opcode::Object:
    case Opcode::GetUpvalue:
    case Opcode::SetUpvalue:
    case Opcode::Class:
    case Opcode::Method:
    case Opcode::StaticMethod:
      return makeByteOperand(chunk, opcode, offset);

    case Opcode::GetProperty:
    case Opcode::SetProperty:
      return makePropertyOperand(chunk, opcode, offset);

    case Opcode::MethodCall:
      return makeMethodCallOperand(chunk, opcode, offset);

    case Opcode::SuperCall:
      return makeSuperCallOperand(chunk, opcode, offset);

    case Opcode::JumpIfFalse:
    case Opcode::Jump:
      return makeForwardJump(chunk, opcode, offset);

    case Opcode::Loop:
      return makeLoopJump(chunk, opcode, offset);

    case Opcode::Closure:
      return makeClosureOperand(chunk, opcode, offset);
  }

  throw RuntimeError("unknown opcode: " + std::to_string(rawOpcode));
}

BytecodeVerificationResult verifyBytecode(const Chunk& chunk) {
  BytecodeVerificationResult result;

  try {
    std::vector<DecodedInstruction> instructions;
    std::size_t offset = 0;
    // 第一遍：先收集所有指令、指令边界
    while (offset < chunk.code().size()) {
      DecodedInstruction instruction = decodeInstruction(chunk, offset);
      instructions.push_back(instruction);
      result.instructionOffsets.push_back(offset);
      offset = instruction.nextOffset;
    }

    // 第二遍：再验证指令格式、跳转目标是否正好落在指令起点。
    for (const DecodedInstruction& instruction : instructions) {
      if (!verifyInstructionOperands(chunk, instruction, result.error)) {
        result.valid = false;
        return result;
      }

      if (instruction.hasJumpTarget &&
          !jumpTargetIsInstructionBoundary(result.instructionOffsets, instruction.jumpTarget)) {
        result.error =
            std::string(opcodeName(instruction.opcode)) + " jump target is not an instruction";
        result.valid = false;
        return result;
      }
    }
  } catch (const RuntimeError& error) {
    result.error = error.what();
    result.valid = false;
    return result;
  }

  result.valid = true;
  result.error.clear();
  return result;
}

}  // namespace minijs
