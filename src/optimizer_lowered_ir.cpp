#include "minijs/optimizer_ir.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace minijs {
namespace {

bool loweredProducesValue(OptimizerLoweredOpcode opcode) {
  switch (opcode) {
    case OptimizerLoweredOpcode::GuardNumber:
    case OptimizerLoweredOpcode::BranchNumber:
    case OptimizerLoweredOpcode::BranchBoolean:
    case OptimizerLoweredOpcode::Jump:
    case OptimizerLoweredOpcode::Return:
      return false;
    default:
      return true;
  }
}

bool isLoweredTerminator(OptimizerLoweredOpcode opcode) {
  return opcode == OptimizerLoweredOpcode::BranchNumber ||
         opcode == OptimizerLoweredOpcode::BranchBoolean ||
         opcode == OptimizerLoweredOpcode::Jump ||
         opcode == OptimizerLoweredOpcode::Return;
}

OptimizerRepresentation representationFor(OptimizerType type) {
  return type == OptimizerType::Number ? OptimizerRepresentation::Number
                                      : OptimizerRepresentation::Boolean;
}

}  // namespace

OptimizerLoweringResult lowerOptimizerNumberIr(
    const OptimizerIrFunction& function, const Chunk& constants,
    const OptimizerTypeAnalysisResult& types) {
  OptimizerLoweringResult result;
  const OptimizerIrVerificationResult verified = verifyOptimizerIr(function);
  if (!verified.valid) {
    result.error = "invalid input IR: " + verified.error;
    return result;
  }
  if (!types.valid || types.types.size() != function.valueCount) {
    result.error = "lowering requires a valid type for every high-level value";
    return result;
  }

  result.function.entry = function.entry;
  result.function.parameterCount = function.parameterCount;
  result.function.blocks.resize(function.blocks.size());
  std::vector<OptimizerValueId> loweredValue(function.valueCount, InvalidOptimizerValue);
  std::vector<OptimizerValueId> taggedParameter(function.valueCount, InvalidOptimizerValue);
  auto newValue = [&](OptimizerRepresentation representation) {
    const OptimizerValueId id = result.function.valueCount++;
    result.function.representations.push_back(representation);
    return id;
  };

  // Allocate IDs before emitting instructions so loop Phi inputs may point to
  // definitions in blocks visited later.
  for (const OptimizerIrBlock& block : function.blocks) {
    for (const OptimizerIrInstruction& instruction : block.instructions) {
      if (instruction.result == InvalidOptimizerValue) {
        continue;
      }
      if (instruction.opcode == OptimizerIrOpcode::Parameter) {
        taggedParameter[instruction.result] = newValue(OptimizerRepresentation::Tagged);
        loweredValue[instruction.result] = newValue(OptimizerRepresentation::Number);
      } else if (types.types[instruction.result] == OptimizerType::Number ||
                 types.types[instruction.result] == OptimizerType::Boolean) {
        loweredValue[instruction.result] =
            newValue(representationFor(types.types[instruction.result]));
      } else {
        result.error = "value " + std::to_string(instruction.result) +
                       " has no lowerable representation";
        return result;
      }
    }
  }

  auto mapped = [&](OptimizerValueId value) {
    return value < loweredValue.size() ? loweredValue[value] : InvalidOptimizerValue;
  };
  for (const OptimizerIrBlock& source : function.blocks) {
    OptimizerLoweredBlock& block = result.function.blocks[source.id];
    block.id = source.id;
    block.predecessors = source.predecessors;
    block.successors = source.successors;
    for (const OptimizerIrInstruction& instruction : source.instructions) {
      OptimizerLoweredInstruction lowered;
      lowered.bytecodeOffset = instruction.bytecodeOffset;
      lowered.immediate = instruction.immediate;
      lowered.trueTarget = instruction.trueTarget;
      lowered.falseTarget = instruction.falseTarget;
      for (const OptimizerValueId input : instruction.inputs) {
        lowered.inputs.push_back(mapped(input));
      }

      const auto inputType = [&](std::size_t index) {
        return types.types[instruction.inputs[index]];
      };
      switch (instruction.opcode) {
        case OptimizerIrOpcode::Parameter:
          lowered.opcode = OptimizerLoweredOpcode::Parameter;
          lowered.result = taggedParameter[instruction.result];
          break;
        case OptimizerIrOpcode::GuardNumber: {
          const OptimizerValueId highParameter = instruction.inputs[0];
          if (highParameter >= taggedParameter.size() ||
              taggedParameter[highParameter] == InvalidOptimizerValue) {
            result.error = "Number guard does not reference a parameter";
            return result;
          }
          lowered.opcode = OptimizerLoweredOpcode::GuardNumber;
          lowered.inputs = {taggedParameter[highParameter]};
          block.instructions.push_back(std::move(lowered));
          OptimizerLoweredInstruction unbox;
          unbox.opcode = OptimizerLoweredOpcode::UnboxNumber;
          unbox.result = mapped(highParameter);
          unbox.inputs = {taggedParameter[highParameter]};
          unbox.bytecodeOffset = instruction.bytecodeOffset;
          block.instructions.push_back(std::move(unbox));
          continue;
        }
        case OptimizerIrOpcode::Constant: {
          const Value value = instruction.hasLiteral
                                  ? instruction.literal
                                  : constants.constant(instruction.immediate);
          lowered.result = mapped(instruction.result);
          if (value.isNumber()) {
            lowered.opcode = OptimizerLoweredOpcode::NumberConstant;
            lowered.numberImmediate = value.asNumber();
          } else if (value.isBoolean()) {
            lowered.opcode = OptimizerLoweredOpcode::BooleanConstant;
            lowered.booleanImmediate = value.isTruthy();
          } else {
            result.error = "lowered IR only supports Number/Boolean constants";
            return result;
          }
          break;
        }
        case OptimizerIrOpcode::Phi:
          lowered.result = mapped(instruction.result);
          lowered.opcode = types.types[instruction.result] == OptimizerType::Number
                               ? OptimizerLoweredOpcode::PhiNumber
                               : OptimizerLoweredOpcode::PhiBoolean;
          break;
        case OptimizerIrOpcode::Add:
          lowered.opcode = OptimizerLoweredOpcode::AddNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Sub:
          lowered.opcode = OptimizerLoweredOpcode::SubNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Mul:
          lowered.opcode = OptimizerLoweredOpcode::MulNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Div:
          lowered.opcode = OptimizerLoweredOpcode::DivNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Mod:
          lowered.opcode = OptimizerLoweredOpcode::ModNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Greater:
          lowered.opcode = OptimizerLoweredOpcode::GreaterNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Less:
          lowered.opcode = OptimizerLoweredOpcode::LessNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Negate:
          lowered.opcode = OptimizerLoweredOpcode::NegateNumber;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Equal:
          lowered.opcode = inputType(0) == OptimizerType::Number
                               ? OptimizerLoweredOpcode::EqualNumber
                               : OptimizerLoweredOpcode::EqualBoolean;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Not:
          lowered.opcode = inputType(0) == OptimizerType::Number
                               ? OptimizerLoweredOpcode::NotNumber
                               : OptimizerLoweredOpcode::NotBoolean;
          lowered.result = mapped(instruction.result);
          break;
        case OptimizerIrOpcode::Branch:
          lowered.opcode = inputType(0) == OptimizerType::Number
                               ? OptimizerLoweredOpcode::BranchNumber
                               : OptimizerLoweredOpcode::BranchBoolean;
          break;
        case OptimizerIrOpcode::Jump:
          lowered.opcode = OptimizerLoweredOpcode::Jump;
          break;
        case OptimizerIrOpcode::Return: {
          OptimizerLoweredInstruction box;
          box.opcode = inputType(0) == OptimizerType::Number
                           ? OptimizerLoweredOpcode::BoxNumber
                           : OptimizerLoweredOpcode::BoxBoolean;
          box.result = newValue(OptimizerRepresentation::Tagged);
          box.inputs = {mapped(instruction.inputs[0])};
          box.bytecodeOffset = instruction.bytecodeOffset;
          block.instructions.push_back(std::move(box));
          lowered.opcode = OptimizerLoweredOpcode::Return;
          lowered.inputs = {result.function.valueCount - 1};
          break;
        }
      }
      block.instructions.push_back(std::move(lowered));
    }
  }

  const OptimizerLoweredVerificationResult loweredVerified =
      verifyOptimizerLoweredIr(result.function);
  if (!loweredVerified.valid) {
    result.error = loweredVerified.error;
    return result;
  }
  result.valid = true;
  return result;
}

OptimizerLoweredVerificationResult verifyOptimizerLoweredIr(
    const OptimizerLoweredFunction& function) {
  OptimizerLoweredVerificationResult result;
  auto fail = [&](std::string error) {
    result.error = std::move(error);
    return result;
  };
  if (function.blocks.empty() || function.entry >= function.blocks.size()) {
    return fail("lowered IR has no valid entry block");
  }
  if (function.representations.size() != function.valueCount) {
    return fail("lowered IR representation table size does not match value count");
  }
  std::vector<bool> defined(function.valueCount, false);
  for (std::size_t index = 0; index < function.blocks.size(); ++index) {
    const OptimizerLoweredBlock& block = function.blocks[index];
    if (block.id != index) {
      return fail("lowered IR block " + std::to_string(index) + " has the wrong id");
    }
    for (const OptimizerBlockId successor : block.successors) {
      if (successor >= function.blocks.size()) return fail("lowered successor is out of bounds");
      const auto& predecessors = function.blocks[successor].predecessors;
      if (std::find(predecessors.begin(), predecessors.end(), block.id) == predecessors.end()) {
        return fail("lowered CFG edge is missing reverse predecessor");
      }
    }
    for (std::size_t i = 0; i < block.instructions.size(); ++i) {
      const auto& instruction = block.instructions[i];
      if (isLoweredTerminator(instruction.opcode) != (i + 1 == block.instructions.size())) {
        return fail("lowered terminator must be the final instruction");
      }
      if (loweredProducesValue(instruction.opcode)) {
        if (instruction.result >= function.valueCount || defined[instruction.result]) {
          return fail("lowered value definition is invalid or duplicated");
        }
        defined[instruction.result] = true;
      } else if (instruction.result != InvalidOptimizerValue) {
        return fail("lowered non-value instruction unexpectedly has a result");
      }
    }
  }
  auto rep = [&](OptimizerValueId value) { return function.representations[value]; };
  auto expectInputs = [&](const OptimizerLoweredInstruction& instruction,
                          std::size_t count, OptimizerRepresentation representation) {
    if (instruction.inputs.size() != count) return false;
    for (const OptimizerValueId input : instruction.inputs) {
      if (input >= function.valueCount || !defined[input] || rep(input) != representation) {
        return false;
      }
    }
    return true;
  };
  for (const OptimizerLoweredBlock& block : function.blocks) {
    for (const auto& instruction : block.instructions) {
      const auto resultIs = [&](OptimizerRepresentation expected) {
        return loweredProducesValue(instruction.opcode) && rep(instruction.result) == expected;
      };
      switch (instruction.opcode) {
        case OptimizerLoweredOpcode::Parameter:
          if (!instruction.inputs.empty() || !resultIs(OptimizerRepresentation::Tagged) ||
              instruction.immediate >= function.parameterCount) return fail("invalid lowered Parameter");
          break;
        case OptimizerLoweredOpcode::GuardNumber:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Tagged)) return fail("invalid lowered GuardNumber");
          break;
        case OptimizerLoweredOpcode::UnboxNumber:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Tagged) || !resultIs(OptimizerRepresentation::Number)) return fail("invalid lowered UnboxNumber");
          break;
        case OptimizerLoweredOpcode::NumberConstant:
          if (!instruction.inputs.empty() || !resultIs(OptimizerRepresentation::Number)) return fail("invalid lowered NumberConstant");
          break;
        case OptimizerLoweredOpcode::BooleanConstant:
          if (!instruction.inputs.empty() || !resultIs(OptimizerRepresentation::Boolean)) return fail("invalid lowered BooleanConstant");
          break;
        case OptimizerLoweredOpcode::PhiNumber:
          if (instruction.inputs.size() != block.predecessors.size() || !expectInputs(instruction, instruction.inputs.size(), OptimizerRepresentation::Number) || !resultIs(OptimizerRepresentation::Number)) return fail("invalid lowered PhiNumber");
          break;
        case OptimizerLoweredOpcode::PhiBoolean:
          if (instruction.inputs.size() != block.predecessors.size() || !expectInputs(instruction, instruction.inputs.size(), OptimizerRepresentation::Boolean) || !resultIs(OptimizerRepresentation::Boolean)) return fail("invalid lowered PhiBoolean");
          break;
        case OptimizerLoweredOpcode::AddNumber:
        case OptimizerLoweredOpcode::SubNumber:
        case OptimizerLoweredOpcode::MulNumber:
        case OptimizerLoweredOpcode::DivNumber:
        case OptimizerLoweredOpcode::ModNumber:
          if (!expectInputs(instruction, 2, OptimizerRepresentation::Number) || !resultIs(OptimizerRepresentation::Number)) return fail("invalid lowered numeric binary operation");
          break;
        case OptimizerLoweredOpcode::NegateNumber:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Number) || !resultIs(OptimizerRepresentation::Number)) return fail("invalid lowered NegateNumber");
          break;
        case OptimizerLoweredOpcode::EqualNumber:
        case OptimizerLoweredOpcode::GreaterNumber:
        case OptimizerLoweredOpcode::LessNumber:
          if (!expectInputs(instruction, 2, OptimizerRepresentation::Number) || !resultIs(OptimizerRepresentation::Boolean)) return fail("invalid lowered numeric comparison");
          break;
        case OptimizerLoweredOpcode::EqualBoolean:
          if (!expectInputs(instruction, 2, OptimizerRepresentation::Boolean) || !resultIs(OptimizerRepresentation::Boolean)) return fail("invalid lowered Boolean equality");
          break;
        case OptimizerLoweredOpcode::NotNumber:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Number) || !resultIs(OptimizerRepresentation::Boolean)) return fail("invalid lowered NotNumber");
          break;
        case OptimizerLoweredOpcode::NotBoolean:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Boolean) || !resultIs(OptimizerRepresentation::Boolean)) return fail("invalid lowered NotBoolean");
          break;
        case OptimizerLoweredOpcode::BranchNumber:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Number) || instruction.trueTarget >= function.blocks.size() || instruction.falseTarget >= function.blocks.size()) return fail("invalid lowered BranchNumber");
          break;
        case OptimizerLoweredOpcode::BranchBoolean:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Boolean) || instruction.trueTarget >= function.blocks.size() || instruction.falseTarget >= function.blocks.size()) return fail("invalid lowered BranchBoolean");
          break;
        case OptimizerLoweredOpcode::Jump:
          if (!instruction.inputs.empty() || instruction.trueTarget >= function.blocks.size()) return fail("invalid lowered Jump");
          break;
        case OptimizerLoweredOpcode::BoxNumber:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Number) || !resultIs(OptimizerRepresentation::Tagged)) return fail("invalid lowered BoxNumber");
          break;
        case OptimizerLoweredOpcode::BoxBoolean:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Boolean) || !resultIs(OptimizerRepresentation::Tagged)) return fail("invalid lowered BoxBoolean");
          break;
        case OptimizerLoweredOpcode::Return:
          if (!expectInputs(instruction, 1, OptimizerRepresentation::Tagged)) return fail("invalid lowered Return");
          break;
      }
    }
  }
  result.valid = true;
  return result;
}

OptimizerLoweredExecutionResult executeOptimizerLoweredIr(
    const OptimizerLoweredFunction& function,
    const std::vector<Value>& arguments, std::size_t stepLimit) {
  OptimizerLoweredExecutionResult result;
  const auto verified = verifyOptimizerLoweredIr(function);
  if (!verified.valid) { result.error = verified.error; return result; }
  if (arguments.size() != function.parameterCount) {
    result.error = "lowered IR argument count mismatch";
    return result;
  }
  std::vector<Value> tagged(function.valueCount);
  std::vector<double> numbers(function.valueCount, 0.0);
  std::vector<bool> booleans(function.valueCount, false);
  OptimizerBlockId blockId = function.entry;
  OptimizerBlockId predecessor = static_cast<OptimizerBlockId>(function.blocks.size());
  std::size_t steps = 0;
  while (steps++ < stepLimit) {
    const OptimizerLoweredBlock& block = function.blocks[blockId];
    std::size_t phiInput = 0;
    if (!block.predecessors.empty()) {
      const auto found = std::find(block.predecessors.begin(), block.predecessors.end(), predecessor);
      if (found == block.predecessors.end()) { result.error = "lowered control flow arrived from a non-predecessor"; return result; }
      phiInput = static_cast<std::size_t>(found - block.predecessors.begin());
    }
    bool transferred = false;
    for (const auto& instruction : block.instructions) {
      switch (instruction.opcode) {
        case OptimizerLoweredOpcode::Parameter: tagged[instruction.result] = arguments[instruction.immediate]; break;
        case OptimizerLoweredOpcode::GuardNumber:
          if (!tagged[instruction.inputs[0]].isNumber()) { result.guardFailed = true; result.deoptId = instruction.deoptId; result.error = "Number guard failed for parameter " + std::to_string(instruction.immediate); return result; }
          break;
        case OptimizerLoweredOpcode::UnboxNumber: numbers[instruction.result] = tagged[instruction.inputs[0]].asNumber(); break;
        case OptimizerLoweredOpcode::NumberConstant: numbers[instruction.result] = instruction.numberImmediate; break;
        case OptimizerLoweredOpcode::BooleanConstant: booleans[instruction.result] = instruction.booleanImmediate; break;
        case OptimizerLoweredOpcode::PhiNumber: numbers[instruction.result] = numbers[instruction.inputs[phiInput]]; break;
        case OptimizerLoweredOpcode::PhiBoolean: booleans[instruction.result] = booleans[instruction.inputs[phiInput]]; break;
        case OptimizerLoweredOpcode::AddNumber: numbers[instruction.result] = numbers[instruction.inputs[0]] + numbers[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::SubNumber: numbers[instruction.result] = numbers[instruction.inputs[0]] - numbers[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::MulNumber: numbers[instruction.result] = numbers[instruction.inputs[0]] * numbers[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::DivNumber:
          if (numbers[instruction.inputs[1]] == 0.0) { result.error = "division by zero"; return result; }
          numbers[instruction.result] = numbers[instruction.inputs[0]] / numbers[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::ModNumber:
          if (numbers[instruction.inputs[1]] == 0.0) { result.error = "modulo by zero"; return result; }
          numbers[instruction.result] = std::fmod(numbers[instruction.inputs[0]], numbers[instruction.inputs[1]]); break;
        case OptimizerLoweredOpcode::NegateNumber: numbers[instruction.result] = -numbers[instruction.inputs[0]]; break;
        case OptimizerLoweredOpcode::EqualNumber: booleans[instruction.result] = numbers[instruction.inputs[0]] == numbers[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::EqualBoolean: booleans[instruction.result] = booleans[instruction.inputs[0]] == booleans[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::GreaterNumber: booleans[instruction.result] = numbers[instruction.inputs[0]] > numbers[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::LessNumber: booleans[instruction.result] = numbers[instruction.inputs[0]] < numbers[instruction.inputs[1]]; break;
        case OptimizerLoweredOpcode::NotNumber: booleans[instruction.result] = numbers[instruction.inputs[0]] == 0.0; break;
        case OptimizerLoweredOpcode::NotBoolean: booleans[instruction.result] = !booleans[instruction.inputs[0]]; break;
        case OptimizerLoweredOpcode::BranchNumber:
          predecessor = blockId; blockId = numbers[instruction.inputs[0]] != 0.0 ? instruction.trueTarget : instruction.falseTarget; transferred = true; break;
        case OptimizerLoweredOpcode::BranchBoolean:
          predecessor = blockId; blockId = booleans[instruction.inputs[0]] ? instruction.trueTarget : instruction.falseTarget; transferred = true; break;
        case OptimizerLoweredOpcode::Jump: predecessor = blockId; blockId = instruction.trueTarget; transferred = true; break;
        case OptimizerLoweredOpcode::BoxNumber: tagged[instruction.result] = Value(numbers[instruction.inputs[0]]); break;
        case OptimizerLoweredOpcode::BoxBoolean: tagged[instruction.result] = Value(booleans[instruction.inputs[0]]); break;
        case OptimizerLoweredOpcode::Return: result.completed = true; result.value = tagged[instruction.inputs[0]]; return result;
      }
      if (transferred) break;
    }
    if (!transferred) { result.error = "lowered block did not transfer control"; return result; }
  }
  result.error = "lowered IR step limit exceeded";
  return result;
}

}  // namespace minijs
