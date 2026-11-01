#include "minijs/optimizer_ir.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <set>
#include <string>

#include "minijs/runtime_error.h"

namespace minijs {
namespace {

bool isTerminator(Opcode opcode) {
  return opcode == Opcode::Return || opcode == Opcode::Jump || opcode == Opcode::Loop ||
         opcode == Opcode::JumpIfFalse;
}

void addUnique(std::vector<OptimizerBlockId>& values, OptimizerBlockId value) {
  if (std::find(values.begin(), values.end(), value) == values.end()) {
    values.push_back(value);
  }
}

OptimizerCfgResult failure(std::string error) {
  OptimizerCfgResult result;
  result.error = std::move(error);
  return result;
}

struct StackEffect {
  std::size_t required = 0;
  int delta = 0;
  bool supported = true;
};

StackEffect stackEffect(Opcode opcode) {
  switch (opcode) {
    case Opcode::Constant:
    case Opcode::GetLocal:
      return {0, 1, true};
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::Div:
    case Opcode::Mod:
    case Opcode::Equal:
    case Opcode::Greater:
    case Opcode::Less:
      return {2, -1, true};
    case Opcode::Negate:
    case Opcode::Not:
    case Opcode::SetLocal:
    case Opcode::JumpIfFalse:
      return {1, 0, true};
    case Opcode::Pop:
    case Opcode::Return:
      return {1, -1, true};
    case Opcode::Jump:
    case Opcode::Loop:
      return {0, 0, true};
    default:
      return {0, 0, false};
  }
}

OptimizerIrOpcode irOpcodeFor(Opcode opcode) {
  switch (opcode) {
    case Opcode::Add: return OptimizerIrOpcode::Add;
    case Opcode::Sub: return OptimizerIrOpcode::Sub;
    case Opcode::Mul: return OptimizerIrOpcode::Mul;
    case Opcode::Div: return OptimizerIrOpcode::Div;
    case Opcode::Mod: return OptimizerIrOpcode::Mod;
    case Opcode::Negate: return OptimizerIrOpcode::Negate;
    case Opcode::Equal: return OptimizerIrOpcode::Equal;
    case Opcode::Greater: return OptimizerIrOpcode::Greater;
    case Opcode::Less: return OptimizerIrOpcode::Less;
    case Opcode::Not: return OptimizerIrOpcode::Not;
    default: return OptimizerIrOpcode::Constant;
  }
}

bool producesValue(OptimizerIrOpcode opcode) {
  return opcode != OptimizerIrOpcode::GuardNumber && opcode != OptimizerIrOpcode::Branch &&
         opcode != OptimizerIrOpcode::Jump && opcode != OptimizerIrOpcode::Return;
}

}  // namespace

OptimizerCfgResult buildOptimizerControlFlowGraph(const Chunk& chunk) {
  const BytecodeVerificationResult bytecode = verifyBytecode(chunk);
  if (!bytecode.valid) {
    return failure("invalid bytecode: " + bytecode.error);
  }
  if (bytecode.instructionOffsets.empty()) {
    return failure("optimizer CFG requires non-empty bytecode");
  }

  std::vector<DecodedInstruction> decoded;
  decoded.reserve(bytecode.instructionOffsets.size());
  std::set<std::size_t> leaders{0};
  for (const std::size_t offset : bytecode.instructionOffsets) {
    DecodedInstruction instruction = decodeInstruction(chunk, offset);
    if (instruction.hasJumpTarget) {
      leaders.insert(instruction.jumpTarget);
    }
    if (isTerminator(instruction.opcode) && instruction.nextOffset < chunk.code().size()) {
      leaders.insert(instruction.nextOffset);
    }
    decoded.push_back(std::move(instruction));
  }

  OptimizerCfgResult result;
  std::map<std::size_t, OptimizerBlockId> blockByStart;
  for (const std::size_t leader : leaders) {
    OptimizerBasicBlock block;
    block.id = static_cast<OptimizerBlockId>(result.graph.blocks.size());
    block.bytecodeStart = leader;
    blockByStart.emplace(leader, block.id);
    result.graph.blocks.push_back(std::move(block));
  }

  std::size_t blockIndex = 0;
  for (const DecodedInstruction& instruction : decoded) {
    while (blockIndex + 1 < result.graph.blocks.size() &&
           instruction.offset >= result.graph.blocks[blockIndex + 1].bytecodeStart) {
      ++blockIndex;
    }
    result.graph.blocks[blockIndex].instructions.push_back(instruction);
  }

  for (std::size_t index = 0; index < result.graph.blocks.size(); ++index) {
    OptimizerBasicBlock& block = result.graph.blocks[index];
    if (block.instructions.empty()) {
      return failure("optimizer CFG contains an empty basic block");
    }
    block.bytecodeEnd = block.instructions.back().nextOffset;
    const DecodedInstruction& last = block.instructions.back();

    auto addSuccessorAt = [&](std::size_t bytecodeOffset) -> bool {
      const auto target = blockByStart.find(bytecodeOffset);
      if (target == blockByStart.end()) {
        return false;
      }
      addUnique(block.successors, target->second);
      return true;
    };

    if (last.opcode == Opcode::Jump || last.opcode == Opcode::Loop) {
      if (!addSuccessorAt(last.jumpTarget)) {
        return failure("optimizer CFG jump target has no basic block");
      }
    } else if (last.opcode == Opcode::JumpIfFalse) {
      if (!addSuccessorAt(last.jumpTarget) ||
          (last.nextOffset < chunk.code().size() && !addSuccessorAt(last.nextOffset))) {
        return failure("optimizer CFG conditional successor has no basic block");
      }
    } else if (last.opcode != Opcode::Return && last.nextOffset < chunk.code().size()) {
      if (!addSuccessorAt(last.nextOffset)) {
        return failure("optimizer CFG fallthrough has no basic block");
      }
    }
  }

  for (const OptimizerBasicBlock& block : result.graph.blocks) {
    for (const OptimizerBlockId successor : block.successors) {
      addUnique(result.graph.blocks[successor].predecessors, block.id);
    }
  }

  const OptimizerCfgVerificationResult verified =
      verifyOptimizerControlFlowGraph(result.graph, chunk.code().size());
  if (!verified.valid) {
    return failure(verified.error);
  }
  result.valid = true;
  return result;
}

OptimizerCfgVerificationResult verifyOptimizerControlFlowGraph(
    const OptimizerControlFlowGraph& graph, std::size_t bytecodeSize) {
  OptimizerCfgVerificationResult result;
  if (graph.blocks.empty()) {
    result.error = "optimizer CFG has no blocks";
    return result;
  }
  if (graph.entry >= graph.blocks.size()) {
    result.error = "optimizer CFG entry is out of bounds";
    return result;
  }

  std::size_t previousEnd = 0;
  for (std::size_t index = 0; index < graph.blocks.size(); ++index) {
    const OptimizerBasicBlock& block = graph.blocks[index];
    if (block.id != index) {
      result.error = "optimizer CFG block id does not match its index";
      return result;
    }
    if (block.instructions.empty() || block.bytecodeStart != block.instructions.front().offset ||
        block.bytecodeEnd != block.instructions.back().nextOffset) {
      result.error = "optimizer CFG block range does not match its instructions";
      return result;
    }
    if (index != 0 && block.bytecodeStart != previousEnd) {
      result.error = "optimizer CFG blocks do not form a contiguous bytecode partition";
      return result;
    }
    previousEnd = block.bytecodeEnd;

    for (std::size_t instructionIndex = 0; instructionIndex < block.instructions.size();
         ++instructionIndex) {
      if (instructionIndex + 1 < block.instructions.size() &&
          isTerminator(block.instructions[instructionIndex].opcode)) {
        result.error = "optimizer CFG terminator appears before end of block";
        return result;
      }
    }
    for (const OptimizerBlockId successor : block.successors) {
      if (successor >= graph.blocks.size()) {
        result.error = "optimizer CFG successor is out of bounds";
        return result;
      }
      const auto& predecessors = graph.blocks[successor].predecessors;
      if (std::find(predecessors.begin(), predecessors.end(), block.id) == predecessors.end()) {
        result.error = "optimizer CFG edge is missing its reverse predecessor";
        return result;
      }
    }
  }
  if (previousEnd != bytecodeSize) {
    result.error = "optimizer CFG does not cover all bytecode";
    return result;
  }

  result.valid = true;
  return result;
}

OptimizerStackAnalysisResult analyzeOptimizerStackDepths(
    const OptimizerControlFlowGraph& graph, std::size_t initialDepth) {
  OptimizerStackAnalysisResult result;
  const OptimizerCfgVerificationResult structural = verifyOptimizerControlFlowGraph(
      graph, graph.blocks.empty() ? 0 : graph.blocks.back().bytecodeEnd);
  if (!structural.valid) {
    result.error = "invalid optimizer CFG: " + structural.error;
    return result;
  }

  result.entryDepths.assign(graph.blocks.size(), OptimizerStackAnalysisResult::unreachableDepth);
  result.exitDepths.assign(graph.blocks.size(), OptimizerStackAnalysisResult::unreachableDepth);
  result.entryDepths[graph.entry] = initialDepth;

  std::queue<OptimizerBlockId> worklist;
  std::vector<bool> queued(graph.blocks.size(), false);
  worklist.push(graph.entry);
  queued[graph.entry] = true;

  while (!worklist.empty()) {
    const OptimizerBlockId blockId = worklist.front();
    worklist.pop();
    queued[blockId] = false;

    const OptimizerBasicBlock& block = graph.blocks[blockId];
    std::size_t depth = result.entryDepths[blockId];
    for (const DecodedInstruction& instruction : block.instructions) {
      const StackEffect effect = stackEffect(instruction.opcode);
      if (!effect.supported) {
        result.error = std::string("optimizer stack analysis does not support ") +
                       opcodeName(instruction.opcode) + " at bytecode offset " +
                       std::to_string(instruction.offset);
        return result;
      }
      if ((instruction.opcode == Opcode::GetLocal || instruction.opcode == Opcode::SetLocal) &&
          instruction.operands[0] >= depth) {
        result.error = std::string(opcodeName(instruction.opcode)) +
                       " local slot is not live at bytecode offset " +
                       std::to_string(instruction.offset);
        return result;
      }
      if (depth < effect.required) {
        result.error = std::string("optimizer stack underflow at ") +
                       opcodeName(instruction.opcode) + " bytecode offset " +
                       std::to_string(instruction.offset);
        return result;
      }
      if (effect.delta < 0) {
        depth -= static_cast<std::size_t>(-effect.delta);
      } else {
        depth += static_cast<std::size_t>(effect.delta);
      }
    }
    result.exitDepths[blockId] = depth;

    for (const OptimizerBlockId successor : block.successors) {
      std::size_t& successorDepth = result.entryDepths[successor];
      if (successorDepth == OptimizerStackAnalysisResult::unreachableDepth) {
        successorDepth = depth;
        if (!queued[successor]) {
          worklist.push(successor);
          queued[successor] = true;
        }
      } else if (successorDepth != depth) {
        result.error = "optimizer stack depth mismatch at block " +
                       std::to_string(successor) + ": expected " +
                       std::to_string(successorDepth) + ", got " + std::to_string(depth);
        return result;
      }
    }
  }

  result.valid = true;
  return result;
}

OptimizerIrResult buildOptimizerIr(const Chunk& chunk, std::size_t initialDepth) {
  OptimizerIrResult result;
  const OptimizerCfgResult cfg = buildOptimizerControlFlowGraph(chunk);
  if (!cfg.valid) {
    result.error = cfg.error;
    return result;
  }
  const OptimizerStackAnalysisResult stack =
      analyzeOptimizerStackDepths(cfg.graph, initialDepth);
  if (!stack.valid) {
    result.error = stack.error;
    return result;
  }

  result.function.entry = cfg.graph.entry;
  result.function.parameterCount = initialDepth;
  result.function.blocks.resize(cfg.graph.blocks.size());
  auto newValue = [&]() { return result.function.valueCount++; };

  // Parameters describe the physical frame slots on the first entry. When the
  // entry is also a loop header, Phi nodes below merge them with backedge values.
  std::vector<OptimizerValueId> parameters;
  parameters.reserve(initialDepth);
  for (std::size_t slot = 0; slot < initialDepth; ++slot) {
    OptimizerIrInstruction parameter;
    parameter.opcode = OptimizerIrOpcode::Parameter;
    parameter.result = newValue();
    parameter.immediate = static_cast<std::uint32_t>(slot);
    parameters.push_back(parameter.result);
    result.function.blocks[cfg.graph.entry].instructions.push_back(std::move(parameter));
  }

  for (const OptimizerBasicBlock& source : cfg.graph.blocks) {
    OptimizerIrBlock& block = result.function.blocks[source.id];
    block.id = source.id;
    block.predecessors = source.predecessors;
    block.successors = source.successors;
    if (stack.entryDepths[source.id] == OptimizerStackAnalysisResult::unreachableDepth) {
      continue;
    }

    const bool plainEntry = source.id == cfg.graph.entry && source.predecessors.empty();
    if (plainEntry) {
      block.entryValues = parameters;
    } else {
      block.entryValues.reserve(stack.entryDepths[source.id]);
      for (std::size_t slot = 0; slot < stack.entryDepths[source.id]; ++slot) {
        OptimizerIrInstruction phi;
        phi.opcode = OptimizerIrOpcode::Phi;
        phi.result = newValue();
        phi.immediate = static_cast<std::uint32_t>(slot);
        block.entryValues.push_back(phi.result);
        block.instructions.push_back(std::move(phi));
      }
    }
  }

  for (const OptimizerBasicBlock& source : cfg.graph.blocks) {
    OptimizerIrBlock& block = result.function.blocks[source.id];
    if (stack.entryDepths[source.id] == OptimizerStackAnalysisResult::unreachableDepth) {
      continue;
    }
    std::vector<OptimizerValueId> values = block.entryValues;

    auto appendValueInstruction = [&](OptimizerIrOpcode opcode, std::size_t offset,
                                      std::vector<OptimizerValueId> inputs,
                                      std::uint32_t immediate = 0) {
      OptimizerIrInstruction instruction;
      instruction.opcode = opcode;
      instruction.result = newValue();
      instruction.inputs = std::move(inputs);
      instruction.immediate = immediate;
      instruction.bytecodeOffset = offset;
      values.push_back(instruction.result);
      block.instructions.push_back(std::move(instruction));
    };

    for (const DecodedInstruction& instruction : source.instructions) {
      switch (instruction.opcode) {
        case Opcode::Constant:
          appendValueInstruction(OptimizerIrOpcode::Constant, instruction.offset, {},
                                 instruction.operands[0]);
          break;
        case Opcode::GetLocal:
          // Reading a local creates no computation; the same SSA value is also
          // pushed as an operand stack value.
          values.push_back(values[instruction.operands[0]]);
          break;
        case Opcode::SetLocal:
          values[instruction.operands[0]] = values.back();
          break;
        case Opcode::Pop:
          values.pop_back();
          break;
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Div:
        case Opcode::Mod:
        case Opcode::Equal:
        case Opcode::Greater:
        case Opcode::Less: {
          const OptimizerValueId right = values.back();
          values.pop_back();
          const OptimizerValueId left = values.back();
          values.pop_back();
          appendValueInstruction(irOpcodeFor(instruction.opcode), instruction.offset,
                                 {left, right});
          break;
        }
        case Opcode::Negate:
        case Opcode::Not: {
          const OptimizerValueId input = values.back();
          values.pop_back();
          appendValueInstruction(irOpcodeFor(instruction.opcode), instruction.offset, {input});
          break;
        }
        case Opcode::JumpIfFalse: {
          OptimizerIrInstruction branch;
          branch.opcode = OptimizerIrOpcode::Branch;
          branch.inputs = {values.back()};
          branch.bytecodeOffset = instruction.offset;
          const OptimizerBlockId jump = block.successors.front();
          const OptimizerBlockId fallthrough = block.successors.back();
          branch.trueTarget = fallthrough;
          branch.falseTarget = jump;
          block.instructions.push_back(std::move(branch));
          break;
        }
        case Opcode::Jump:
        case Opcode::Loop: {
          OptimizerIrInstruction jump;
          jump.opcode = OptimizerIrOpcode::Jump;
          jump.bytecodeOffset = instruction.offset;
          jump.trueTarget = block.successors.front();
          block.instructions.push_back(std::move(jump));
          break;
        }
        case Opcode::Return: {
          OptimizerIrInstruction returnInstruction;
          returnInstruction.opcode = OptimizerIrOpcode::Return;
          returnInstruction.inputs = {values.back()};
          returnInstruction.bytecodeOffset = instruction.offset;
          values.pop_back();
          block.instructions.push_back(std::move(returnInstruction));
          break;
        }
        default:
          result.error = std::string("optimizer IR does not support ") +
                         opcodeName(instruction.opcode);
          return result;
      }
    }

    // A block may end only because the next bytecode offset is a leader. Make
    // that fallthrough explicit so IR execution and lowering never depend on
    // the physical order of blocks.
    const bool hasTerminator = !block.instructions.empty() &&
        (block.instructions.back().opcode == OptimizerIrOpcode::Branch ||
         block.instructions.back().opcode == OptimizerIrOpcode::Jump ||
         block.instructions.back().opcode == OptimizerIrOpcode::Return);
    if (!hasTerminator && block.successors.size() == 1) {
      OptimizerIrInstruction jump;
      jump.opcode = OptimizerIrOpcode::Jump;
      jump.bytecodeOffset = source.bytecodeEnd;
      jump.trueTarget = block.successors.front();
      block.instructions.push_back(std::move(jump));
    }
    block.exitValues = std::move(values);
  }

  // Fill Phi inputs after translating every block so loop backedges are
  // available. The entry block additionally receives its initial parameters.
  for (OptimizerIrBlock& block : result.function.blocks) {
    if (stack.entryDepths[block.id] == OptimizerStackAnalysisResult::unreachableDepth ||
        (block.id == result.function.entry && block.predecessors.empty())) {
      continue;
    }
    std::vector<OptimizerIrInstruction*> phis;
    for (OptimizerIrInstruction& instruction : block.instructions) {
      if (instruction.opcode == OptimizerIrOpcode::Phi) phis.push_back(&instruction);
    }
    if (block.id == result.function.entry) {
      for (std::size_t slot = 0; slot < phis.size(); ++slot) {
        phis[slot]->inputs.push_back(parameters[slot]);
      }
    }
    for (const OptimizerBlockId predecessor : block.predecessors) {
      const auto& predecessorValues = result.function.blocks[predecessor].exitValues;
      for (std::size_t slot = 0; slot < phis.size(); ++slot) {
        phis[slot]->inputs.push_back(predecessorValues[slot]);
      }
    }
  }

  const OptimizerIrVerificationResult verified = verifyOptimizerIr(result.function);
  if (!verified.valid) {
    result.error = verified.error;
    return result;
  }
  result.valid = true;
  return result;
}

OptimizerIrVerificationResult verifyOptimizerIr(const OptimizerIrFunction& function) {
  OptimizerIrVerificationResult result;
  if (function.blocks.empty() || function.entry >= function.blocks.size()) {
    result.error = "optimizer IR has no valid entry block";
    return result;
  }
  std::vector<bool> definitions(function.valueCount, false);
  for (const OptimizerIrBlock& block : function.blocks) {
    if (block.id >= function.blocks.size()) {
      result.error = "optimizer IR block id is out of bounds";
      return result;
    }
    bool sawNonPhi = false;
    for (const OptimizerIrInstruction& instruction : block.instructions) {
      if (instruction.opcode == OptimizerIrOpcode::Phi && sawNonPhi) {
        result.error = "optimizer IR Phi appears after a non-Phi instruction";
        return result;
      }
      if (instruction.opcode != OptimizerIrOpcode::Phi &&
          instruction.opcode != OptimizerIrOpcode::Parameter) {
        sawNonPhi = true;
      }
      if (producesValue(instruction.opcode)) {
        if (instruction.result >= function.valueCount || definitions[instruction.result]) {
          result.error = "optimizer IR value has no unique definition";
          return result;
        }
        definitions[instruction.result] = true;
      } else if (instruction.result != InvalidOptimizerValue) {
        result.error = "optimizer IR terminator unexpectedly defines a value";
        return result;
      }
      for (const OptimizerValueId input : instruction.inputs) {
        if (input >= function.valueCount) {
          result.error = "optimizer IR input value is out of bounds";
          return result;
        }
      }
      if (instruction.opcode == OptimizerIrOpcode::Phi) {
        const std::size_t expected = block.predecessors.size() +
                                     (block.id == function.entry ? 1 : 0);
        if (instruction.inputs.size() != expected) {
          result.error = "optimizer IR Phi input count does not match incoming edges";
          return result;
        }
      }
    }
  }
  for (const OptimizerIrBlock& block : function.blocks) {
    for (const OptimizerIrInstruction& instruction : block.instructions) {
      for (const OptimizerValueId input : instruction.inputs) {
        if (!definitions[input]) {
          result.error = "optimizer IR input references an undefined value id";
          return result;
        }
      }
    }
  }
  result.valid = true;
  return result;
}

OptimizerIrExecutionResult executeOptimizerIr(
    const OptimizerIrFunction& function, const Chunk& constants,
    const std::vector<Value>& arguments, std::size_t stepLimit) {
  OptimizerIrExecutionResult result;
  const OptimizerIrVerificationResult verified = verifyOptimizerIr(function);
  if (!verified.valid) {
    result.error = verified.error;
    return result;
  }
  if (arguments.size() != function.parameterCount) {
    result.error = "optimizer IR argument count mismatch";
    return result;
  }

  try {
    std::vector<Value> values(function.valueCount);
    OptimizerBlockId current = function.entry;
    OptimizerBlockId predecessor = 0;
    bool initialEntry = true;
    std::size_t steps = 0;

    while (steps < stepLimit) {
      const OptimizerIrBlock& block = function.blocks[current];

      // Phi selection is edge-based. Entry Phi nodes reserve input zero for
      // the initial parameter state; subsequent entry visits come from a real
      // backedge and use the corresponding predecessor input.
      std::size_t phiInput = 0;
      if (!(current == function.entry && initialEntry)) {
        const auto incoming =
            std::find(block.predecessors.begin(), block.predecessors.end(), predecessor);
        if (incoming == block.predecessors.end()) {
          throw RuntimeError("optimizer IR entered block from a non-predecessor");
        }
        phiInput = static_cast<std::size_t>(incoming - block.predecessors.begin()) +
                   (current == function.entry ? 1 : 0);
      }

      bool transferred = false;
      for (const OptimizerIrInstruction& instruction : block.instructions) {
        if (++steps > stepLimit) break;
        auto input = [&](std::size_t index) -> const Value& {
          return values[instruction.inputs.at(index)];
        };

        switch (instruction.opcode) {
          case OptimizerIrOpcode::Parameter:
            values[instruction.result] = arguments.at(instruction.immediate);
            break;
          case OptimizerIrOpcode::Constant:
            values[instruction.result] = instruction.hasLiteral
                                             ? instruction.literal
                                             : constants.constant(instruction.immediate);
            break;
          case OptimizerIrOpcode::Phi:
            values[instruction.result] = input(phiInput);
            break;
          case OptimizerIrOpcode::Add:
            if (input(0).isString() || input(1).isString()) {
              values[instruction.result] = Value(input(0).toString() + input(1).toString());
            } else {
              values[instruction.result] =
                  Value(input(0).asNumber() + input(1).asNumber());
            }
            break;
          case OptimizerIrOpcode::Sub:
            values[instruction.result] = Value(input(0).asNumber() - input(1).asNumber());
            break;
          case OptimizerIrOpcode::Mul:
            values[instruction.result] = Value(input(0).asNumber() * input(1).asNumber());
            break;
          case OptimizerIrOpcode::Div: {
            const double divisor = input(1).asNumber();
            if (divisor == 0.0) throw RuntimeError("division by zero");
            values[instruction.result] = Value(input(0).asNumber() / divisor);
            break;
          }
          case OptimizerIrOpcode::Mod: {
            const double divisor = input(1).asNumber();
            if (divisor == 0.0) throw RuntimeError("modulo by zero");
            values[instruction.result] = Value(std::fmod(input(0).asNumber(), divisor));
            break;
          }
          case OptimizerIrOpcode::Negate:
            values[instruction.result] = Value(-input(0).asNumber());
            break;
          case OptimizerIrOpcode::Equal:
            values[instruction.result] = Value(input(0).equals(input(1)));
            break;
          case OptimizerIrOpcode::Greater:
            values[instruction.result] = Value(input(0).asNumber() > input(1).asNumber());
            break;
          case OptimizerIrOpcode::Less:
            values[instruction.result] = Value(input(0).asNumber() < input(1).asNumber());
            break;
          case OptimizerIrOpcode::Not:
            values[instruction.result] = Value(!input(0).isTruthy());
            break;
          case OptimizerIrOpcode::GuardNumber:
            if (!input(0).isNumber()) {
              result.guardFailed = true;
              result.error = "optimizer Number guard failed for parameter " +
                             std::to_string(instruction.immediate);
              return result;
            }
            break;
          case OptimizerIrOpcode::Branch:
            predecessor = current;
            current = input(0).isTruthy() ? instruction.trueTarget : instruction.falseTarget;
            transferred = true;
            break;
          case OptimizerIrOpcode::Jump:
            predecessor = current;
            current = instruction.trueTarget;
            transferred = true;
            break;
          case OptimizerIrOpcode::Return:
            result.value = input(0);
            result.completed = true;
            return result;
        }
        if (transferred) break;
      }

      if (steps >= stepLimit) break;
      if (!transferred) {
        throw RuntimeError("optimizer IR block completed without a terminator");
      }
      initialEntry = false;
    }
    result.error = "optimizer IR step limit exceeded";
  } catch (const std::exception& error) {
    result.error = error.what();
  }
  return result;
}

const char* optimizerTypeName(OptimizerType type) {
  switch (type) {
    case OptimizerType::Bottom: return "Bottom";
    case OptimizerType::Number: return "Number";
    case OptimizerType::Boolean: return "Boolean";
    case OptimizerType::Null: return "Null";
    case OptimizerType::Undefined: return "Undefined";
    case OptimizerType::String: return "String";
    case OptimizerType::Object: return "Object";
    case OptimizerType::Unknown: return "Unknown";
  }
  return "Invalid";
}

namespace {

OptimizerType typeOfConstant(const Value& value) {
  if (value.isNumber()) return OptimizerType::Number;
  if (value.isBoolean()) return OptimizerType::Boolean;
  if (value.isNull()) return OptimizerType::Null;
  if (value.isUndefined()) return OptimizerType::Undefined;
  if (value.isString()) return OptimizerType::String;
  return OptimizerType::Object;
}

OptimizerType joinTypes(OptimizerType left, OptimizerType right) {
  if (left == OptimizerType::Bottom) return right;
  if (right == OptimizerType::Bottom) return left;
  if (left == right) return left;
  return OptimizerType::Unknown;
}

OptimizerType inferInstructionType(const OptimizerIrInstruction& instruction,
                                   const Chunk& constants,
                                   const std::vector<OptimizerType>& parameterTypes,
                                   const std::vector<OptimizerType>& types) {
  auto input = [&](std::size_t index) { return types[instruction.inputs[index]]; };
  auto allInputsKnown = [&]() {
    return std::none_of(instruction.inputs.begin(), instruction.inputs.end(),
                        [&](OptimizerValueId value) {
                          return types[value] == OptimizerType::Bottom;
                        });
  };

  switch (instruction.opcode) {
    case OptimizerIrOpcode::Parameter:
      return parameterTypes[instruction.immediate];
    case OptimizerIrOpcode::Constant:
      return typeOfConstant(instruction.hasLiteral
                                ? instruction.literal
                                : constants.constant(instruction.immediate));
    case OptimizerIrOpcode::Phi: {
      OptimizerType joined = OptimizerType::Bottom;
      for (const OptimizerValueId value : instruction.inputs) {
        joined = joinTypes(joined, types[value]);
      }
      return joined;
    }
    case OptimizerIrOpcode::Add:
      if (!allInputsKnown()) return OptimizerType::Bottom;
      if (input(0) == OptimizerType::Number && input(1) == OptimizerType::Number) {
        return OptimizerType::Number;
      }
      if (input(0) == OptimizerType::String || input(1) == OptimizerType::String) {
        return OptimizerType::String;
      }
      return OptimizerType::Unknown;
    case OptimizerIrOpcode::Sub:
    case OptimizerIrOpcode::Mul:
    case OptimizerIrOpcode::Div:
    case OptimizerIrOpcode::Mod:
      if (!allInputsKnown()) return OptimizerType::Bottom;
      return input(0) == OptimizerType::Number && input(1) == OptimizerType::Number
                 ? OptimizerType::Number
                 : OptimizerType::Unknown;
    case OptimizerIrOpcode::Negate:
      if (!allInputsKnown()) return OptimizerType::Bottom;
      return input(0) == OptimizerType::Number ? OptimizerType::Number
                                               : OptimizerType::Unknown;
    case OptimizerIrOpcode::Equal:
    case OptimizerIrOpcode::Greater:
    case OptimizerIrOpcode::Less:
    case OptimizerIrOpcode::Not:
      return allInputsKnown() ? OptimizerType::Boolean : OptimizerType::Bottom;
    case OptimizerIrOpcode::GuardNumber:
    case OptimizerIrOpcode::Branch:
    case OptimizerIrOpcode::Jump:
    case OptimizerIrOpcode::Return:
      return OptimizerType::Bottom;
  }
  return OptimizerType::Unknown;
}

}  // namespace

OptimizerTypeAnalysisResult inferOptimizerTypes(
    const OptimizerIrFunction& function, const Chunk& constants,
    const std::vector<OptimizerType>& parameterTypes) {
  OptimizerTypeAnalysisResult result;
  const OptimizerIrVerificationResult verified = verifyOptimizerIr(function);
  if (!verified.valid) {
    result.error = verified.error;
    return result;
  }
  if (parameterTypes.size() != function.parameterCount) {
    result.error = "optimizer type analysis parameter count mismatch";
    return result;
  }
  if (std::find(parameterTypes.begin(), parameterTypes.end(), OptimizerType::Bottom) !=
      parameterTypes.end()) {
    result.error = "optimizer parameter type cannot be Bottom";
    return result;
  }

  result.types.assign(function.valueCount, OptimizerType::Bottom);
  std::vector<const OptimizerIrInstruction*> definitions(function.valueCount, nullptr);
  std::vector<std::vector<OptimizerValueId>> dependents(function.valueCount);
  for (const OptimizerIrBlock& block : function.blocks) {
    for (const OptimizerIrInstruction& instruction : block.instructions) {
      if (!producesValue(instruction.opcode)) continue;
      definitions[instruction.result] = &instruction;
      for (const OptimizerValueId input : instruction.inputs) {
        dependents[input].push_back(instruction.result);
      }
    }
  }

  std::queue<OptimizerValueId> worklist;
  std::vector<bool> queued(function.valueCount, false);
  for (OptimizerValueId value = 0; value < function.valueCount; ++value) {
    if (definitions[value] == nullptr) continue;
    worklist.push(value);
    queued[value] = true;
  }

  while (!worklist.empty()) {
    const OptimizerValueId value = worklist.front();
    worklist.pop();
    queued[value] = false;
    const OptimizerIrInstruction* definition = definitions[value];
    const OptimizerType inferred =
        inferInstructionType(*definition, constants, parameterTypes, result.types);
    const OptimizerType previous = result.types[value];
    const OptimizerType next = joinTypes(previous, inferred);
    if (next == previous) continue;
    result.types[value] = next;
    for (const OptimizerValueId dependent : dependents[value]) {
      if (!queued[dependent]) {
        worklist.push(dependent);
        queued[dependent] = true;
      }
    }
  }

  for (OptimizerValueId value = 0; value < function.valueCount; ++value) {
    if (definitions[value] != nullptr && result.types[value] == OptimizerType::Bottom) {
      result.error = "optimizer type analysis did not resolve value " +
                     std::to_string(value);
      return result;
    }
  }
  result.valid = true;
  return result;
}

const char* optimizerEligibilityFailureName(OptimizerEligibilityFailure failure) {
  switch (failure) {
    case OptimizerEligibilityFailure::None: return "None";
    case OptimizerEligibilityFailure::InvalidIr: return "InvalidIr";
    case OptimizerEligibilityFailure::TypeCountMismatch: return "TypeCountMismatch";
    case OptimizerEligibilityFailure::NonNumberParameter: return "NonNumberParameter";
    case OptimizerEligibilityFailure::UnsupportedConstant: return "UnsupportedConstant";
    case OptimizerEligibilityFailure::UnsupportedPhiType: return "UnsupportedPhiType";
    case OptimizerEligibilityFailure::NonNumericOperation: return "NonNumericOperation";
    case OptimizerEligibilityFailure::InvalidConditionType: return "InvalidConditionType";
    case OptimizerEligibilityFailure::UnsupportedReturnType: return "UnsupportedReturnType";
  }
  return "Invalid";
}

namespace {

OptimizerEligibilityResult rejectEligibility(OptimizerEligibilityFailure failure,
                                             const OptimizerIrInstruction* instruction,
                                             std::string detail) {
  OptimizerEligibilityResult result;
  result.failure = failure;
  if (instruction != nullptr) {
    result.bytecodeOffset = instruction->bytecodeOffset;
    result.value = instruction->result;
  }
  result.error = std::string(optimizerEligibilityFailureName(failure)) + ": " + detail;
  return result;
}

bool isPrimitiveOptimizerType(OptimizerType type) {
  return type == OptimizerType::Number || type == OptimizerType::Boolean;
}

}  // namespace

OptimizerEligibilityResult analyzeOptimizerEligibility(
    const OptimizerIrFunction& function, const Chunk& constants,
    const OptimizerTypeAnalysisResult& types) {
  const OptimizerIrVerificationResult verified = verifyOptimizerIr(function);
  if (!verified.valid) {
    return rejectEligibility(OptimizerEligibilityFailure::InvalidIr, nullptr, verified.error);
  }
  if (!types.valid || types.types.size() != function.valueCount) {
    return rejectEligibility(OptimizerEligibilityFailure::TypeCountMismatch, nullptr,
                             "type analysis is invalid or does not cover every SSA value");
  }

  for (const OptimizerIrBlock& block : function.blocks) {
    for (const OptimizerIrInstruction& instruction : block.instructions) {
      auto type = [&](OptimizerValueId value) { return types.types[value]; };
      auto inputType = [&](std::size_t index) { return type(instruction.inputs[index]); };
      switch (instruction.opcode) {
        case OptimizerIrOpcode::Parameter:
          if (type(instruction.result) != OptimizerType::Number) {
            return rejectEligibility(OptimizerEligibilityFailure::NonNumberParameter,
                                     &instruction,
                                     "parameter " + std::to_string(instruction.immediate) +
                                         " is " + optimizerTypeName(type(instruction.result)));
          }
          break;
        case OptimizerIrOpcode::Constant: {
          const OptimizerType constantType = typeOfConstant(
              instruction.hasLiteral ? instruction.literal
                                     : constants.constant(instruction.immediate));
          if (constantType != OptimizerType::Number &&
              constantType != OptimizerType::Boolean) {
            return rejectEligibility(OptimizerEligibilityFailure::UnsupportedConstant,
                                     &instruction,
                                     std::string("constant is ") +
                                         optimizerTypeName(constantType));
          }
          break;
        }
        case OptimizerIrOpcode::Phi:
          if (!isPrimitiveOptimizerType(type(instruction.result))) {
            return rejectEligibility(OptimizerEligibilityFailure::UnsupportedPhiType,
                                     &instruction,
                                     std::string("Phi result is ") +
                                         optimizerTypeName(type(instruction.result)));
          }
          break;
        case OptimizerIrOpcode::Add:
        case OptimizerIrOpcode::Sub:
        case OptimizerIrOpcode::Mul:
        case OptimizerIrOpcode::Div:
        case OptimizerIrOpcode::Mod:
          if (inputType(0) != OptimizerType::Number ||
              inputType(1) != OptimizerType::Number ||
              type(instruction.result) != OptimizerType::Number) {
            return rejectEligibility(OptimizerEligibilityFailure::NonNumericOperation,
                                     &instruction,
                                     "binary arithmetic is not Number-only");
          }
          break;
        case OptimizerIrOpcode::Negate:
          if (inputType(0) != OptimizerType::Number ||
              type(instruction.result) != OptimizerType::Number) {
            return rejectEligibility(OptimizerEligibilityFailure::NonNumericOperation,
                                     &instruction, "Negate is not Number-only");
          }
          break;
        case OptimizerIrOpcode::Greater:
        case OptimizerIrOpcode::Less:
          if (inputType(0) != OptimizerType::Number ||
              inputType(1) != OptimizerType::Number ||
              type(instruction.result) != OptimizerType::Boolean) {
            return rejectEligibility(OptimizerEligibilityFailure::NonNumericOperation,
                                     &instruction,
                                     "ordered comparison is not Number-only");
          }
          break;
        case OptimizerIrOpcode::Equal:
          if (!isPrimitiveOptimizerType(inputType(0)) ||
              inputType(0) != inputType(1) ||
              type(instruction.result) != OptimizerType::Boolean) {
            return rejectEligibility(OptimizerEligibilityFailure::NonNumericOperation,
                                     &instruction,
                                     "equality inputs are not one stable primitive type");
          }
          break;
        case OptimizerIrOpcode::Not:
          if (!isPrimitiveOptimizerType(inputType(0)) ||
              type(instruction.result) != OptimizerType::Boolean) {
            return rejectEligibility(OptimizerEligibilityFailure::InvalidConditionType,
                                     &instruction, "Not input is not Number/Boolean");
          }
          break;
        case OptimizerIrOpcode::GuardNumber:
          if (inputType(0) != OptimizerType::Number) {
            return rejectEligibility(OptimizerEligibilityFailure::NonNumberParameter,
                                     &instruction, "Number guard input is not Number");
          }
          break;
        case OptimizerIrOpcode::Branch:
          if (!isPrimitiveOptimizerType(inputType(0))) {
            return rejectEligibility(OptimizerEligibilityFailure::InvalidConditionType,
                                     &instruction,
                                     std::string("branch condition is ") +
                                         optimizerTypeName(inputType(0)));
          }
          break;
        case OptimizerIrOpcode::Return:
          if (!isPrimitiveOptimizerType(inputType(0))) {
            return rejectEligibility(OptimizerEligibilityFailure::UnsupportedReturnType,
                                     &instruction,
                                     std::string("return value is ") +
                                         optimizerTypeName(inputType(0)));
          }
          break;
        case OptimizerIrOpcode::Jump:
          break;
      }
    }
  }

  OptimizerEligibilityResult result;
  result.eligible = true;
  return result;
}

OptimizerGuardInsertionResult insertOptimizerNumberGuards(
    const OptimizerIrFunction& function, const OptimizerEligibilityResult& eligibility) {
  OptimizerGuardInsertionResult result;
  if (!eligibility.eligible) {
    result.error = "cannot insert optimizer guards for an ineligible function: " +
                   eligibility.error;
    return result;
  }
  const OptimizerIrVerificationResult verified = verifyOptimizerIr(function);
  if (!verified.valid) {
    result.error = verified.error;
    return result;
  }

  result.function = function;
  const OptimizerBlockId oldEntryId = result.function.entry;
  OptimizerIrBlock& oldEntry = result.function.blocks[oldEntryId];
  std::vector<OptimizerIrInstruction> parameters;
  for (auto instruction = oldEntry.instructions.begin();
       instruction != oldEntry.instructions.end();) {
    if (instruction->opcode == OptimizerIrOpcode::Parameter) {
      parameters.push_back(std::move(*instruction));
      instruction = oldEntry.instructions.erase(instruction);
    } else {
      ++instruction;
    }
  }

  std::vector<OptimizerIrInstruction> guards;
  for (const OptimizerIrInstruction& instruction : parameters) {
    OptimizerIrInstruction guard;
    guard.opcode = OptimizerIrOpcode::GuardNumber;
    guard.inputs = {instruction.result};
    guard.immediate = instruction.immediate;
    guard.bytecodeOffset = 0;
    guards.push_back(std::move(guard));
  }
  result.guardCount = guards.size();

  // A synthetic entry/preheader keeps guards out of loop headers. The old
  // entry's Phi nodes already reserve their first input for initial parameters;
  // that input now corresponds to this explicit predecessor edge.
  OptimizerIrBlock preheader;
  preheader.id = static_cast<OptimizerBlockId>(result.function.blocks.size());
  preheader.successors = {oldEntryId};
  for (const OptimizerIrInstruction& parameter : parameters) {
    preheader.entryValues.push_back(parameter.result);
    preheader.exitValues.push_back(parameter.result);
  }
  preheader.instructions = std::move(parameters);
  preheader.instructions.insert(preheader.instructions.end(), guards.begin(), guards.end());
  OptimizerIrInstruction jump;
  jump.opcode = OptimizerIrOpcode::Jump;
  jump.trueTarget = oldEntryId;
  preheader.instructions.push_back(std::move(jump));

  oldEntry.predecessors.insert(oldEntry.predecessors.begin(), preheader.id);
  result.function.blocks.push_back(std::move(preheader));
  result.function.entry = static_cast<OptimizerBlockId>(result.function.blocks.size() - 1);

  const OptimizerIrVerificationResult guarded = verifyOptimizerIr(result.function);
  if (!guarded.valid) {
    result.error = guarded.error;
    return result;
  }
  result.valid = true;
  return result;
}

OptimizerPassResult optimizeOptimizerIr(const OptimizerIrFunction& function,
                                        const Chunk& constants) {
  OptimizerPassResult result;
  const OptimizerIrVerificationResult initial = verifyOptimizerIr(function);
  if (!initial.valid) {
    result.error = initial.error;
    return result;
  }
  result.function = function;

  std::vector<OptimizerValueId> aliases(result.function.valueCount);
  for (OptimizerValueId value = 0; value < result.function.valueCount; ++value) {
    aliases[value] = value;
  }
  auto resolve = [&](OptimizerValueId value) {
    while (aliases[value] != value) value = aliases[value];
    return value;
  };
  auto rewriteUses = [&]() {
    for (OptimizerIrBlock& block : result.function.blocks) {
      for (OptimizerIrInstruction& instruction : block.instructions) {
        for (OptimizerValueId& input : instruction.inputs) input = resolve(input);
      }
      for (OptimizerValueId& value : block.entryValues) value = resolve(value);
      for (OptimizerValueId& value : block.exitValues) value = resolve(value);
    }
  };
  auto removeRedundantPhis = [&]() {
    bool removedAny = false;
    for (OptimizerIrBlock& block : result.function.blocks) {
      for (auto instruction = block.instructions.begin(); instruction != block.instructions.end();) {
        if (instruction->opcode != OptimizerIrOpcode::Phi) {
          ++instruction;
          continue;
        }
        OptimizerValueId replacement = InvalidOptimizerValue;
        bool conflicting = false;
        for (OptimizerValueId input : instruction->inputs) {
          input = resolve(input);
          if (input == instruction->result) continue;
          if (replacement == InvalidOptimizerValue) replacement = input;
          else if (replacement != input) conflicting = true;
        }
        if (!conflicting && replacement != InvalidOptimizerValue) {
          aliases[instruction->result] = replacement;
          instruction = block.instructions.erase(instruction);
          ++result.stats.phisRemoved;
          removedAny = true;
        } else {
          ++instruction;
        }
      }
    }
    if (removedAny) rewriteUses();
    return removedAny;
  };

  while (removeRedundantPhis()) {
  }

  std::vector<const OptimizerIrInstruction*> definitions(result.function.valueCount, nullptr);
  auto rebuildDefinitions = [&]() {
    std::fill(definitions.begin(), definitions.end(), nullptr);
    for (const OptimizerIrBlock& block : result.function.blocks) {
      for (const OptimizerIrInstruction& instruction : block.instructions) {
        if (producesValue(instruction.opcode)) definitions[instruction.result] = &instruction;
      }
    }
  };
  rebuildDefinitions();
  auto constantValue = [&](OptimizerValueId value, Value& output) {
    const OptimizerIrInstruction* definition = definitions[resolve(value)];
    if (definition == nullptr || definition->opcode != OptimizerIrOpcode::Constant) return false;
    output = definition->hasLiteral ? definition->literal
                                    : constants.constant(definition->immediate);
    return true;
  };

  for (OptimizerIrBlock& block : result.function.blocks) {
    for (OptimizerIrInstruction& instruction : block.instructions) {
      if (!producesValue(instruction.opcode) ||
          instruction.opcode == OptimizerIrOpcode::Parameter ||
          instruction.opcode == OptimizerIrOpcode::Constant ||
          instruction.opcode == OptimizerIrOpcode::Phi) {
        continue;
      }
      std::vector<Value> inputs;
      bool allConstant = true;
      for (const OptimizerValueId input : instruction.inputs) {
        Value value;
        if (!constantValue(input, value)) {
          allConstant = false;
          break;
        }
        inputs.push_back(std::move(value));
      }
      if (!allConstant) continue;

      try {
        Value folded;
        bool canFold = true;
        switch (instruction.opcode) {
          case OptimizerIrOpcode::Add:
            folded = (inputs[0].isString() || inputs[1].isString())
                         ? Value(inputs[0].toString() + inputs[1].toString())
                         : Value(inputs[0].asNumber() + inputs[1].asNumber());
            break;
          case OptimizerIrOpcode::Sub:
            folded = Value(inputs[0].asNumber() - inputs[1].asNumber());
            break;
          case OptimizerIrOpcode::Mul:
            folded = Value(inputs[0].asNumber() * inputs[1].asNumber());
            break;
          case OptimizerIrOpcode::Div:
            if (inputs[1].asNumber() == 0.0) canFold = false;
            else folded = Value(inputs[0].asNumber() / inputs[1].asNumber());
            break;
          case OptimizerIrOpcode::Mod:
            if (inputs[1].asNumber() == 0.0) canFold = false;
            else folded = Value(std::fmod(inputs[0].asNumber(), inputs[1].asNumber()));
            break;
          case OptimizerIrOpcode::Negate:
            folded = Value(-inputs[0].asNumber());
            break;
          case OptimizerIrOpcode::Equal:
            folded = Value(inputs[0].equals(inputs[1]));
            break;
          case OptimizerIrOpcode::Greater:
            folded = Value(inputs[0].asNumber() > inputs[1].asNumber());
            break;
          case OptimizerIrOpcode::Less:
            folded = Value(inputs[0].asNumber() < inputs[1].asNumber());
            break;
          case OptimizerIrOpcode::Not:
            folded = Value(!inputs[0].isTruthy());
            break;
          default:
            canFold = false;
            break;
        }
        if (canFold) {
          instruction.opcode = OptimizerIrOpcode::Constant;
          instruction.inputs.clear();
          instruction.hasLiteral = true;
          instruction.literal = std::move(folded);
          ++result.stats.constantsFolded;
        }
      } catch (const RuntimeError&) {
        // Preserve the original operation so runtime error ordering is unchanged.
      }
    }
  }

  rebuildDefinitions();
  for (OptimizerIrBlock& block : result.function.blocks) {
    if (block.instructions.empty()) continue;
    OptimizerIrInstruction& terminator = block.instructions.back();
    if (terminator.opcode != OptimizerIrOpcode::Branch) continue;
    Value condition;
    if (!constantValue(terminator.inputs[0], condition)) continue;
    const OptimizerBlockId chosen = condition.isTruthy() ? terminator.trueTarget
                                                         : terminator.falseTarget;
    const OptimizerBlockId rejected = condition.isTruthy() ? terminator.falseTarget
                                                           : terminator.trueTarget;
    if (chosen != rejected) {
      OptimizerIrBlock& rejectedBlock = result.function.blocks[rejected];
      const auto predecessor = std::find(rejectedBlock.predecessors.begin(),
                                         rejectedBlock.predecessors.end(), block.id);
      if (predecessor != rejectedBlock.predecessors.end()) {
        const std::size_t predecessorIndex =
            static_cast<std::size_t>(predecessor - rejectedBlock.predecessors.begin());
        const std::size_t phiIndex = predecessorIndex +
            (rejectedBlock.id == result.function.entry ? 1 : 0);
        for (OptimizerIrInstruction& instruction : rejectedBlock.instructions) {
          if (instruction.opcode != OptimizerIrOpcode::Phi) break;
          if (phiIndex < instruction.inputs.size()) {
            instruction.inputs.erase(instruction.inputs.begin() + phiIndex);
          }
        }
        rejectedBlock.predecessors.erase(predecessor);
      }
    }
    block.successors = {chosen};
    terminator.opcode = OptimizerIrOpcode::Jump;
    terminator.inputs.clear();
    terminator.trueTarget = chosen;
    terminator.falseTarget = 0;
    ++result.stats.branchesFolded;
  }

  while (removeRedundantPhis()) {
  }

  rebuildDefinitions();
  std::vector<bool> live(result.function.valueCount, false);
  std::queue<OptimizerValueId> liveWorklist;
  for (const OptimizerIrBlock& block : result.function.blocks) {
    for (const OptimizerIrInstruction& instruction : block.instructions) {
      if (producesValue(instruction.opcode)) continue;
      for (const OptimizerValueId input : instruction.inputs) {
        const OptimizerValueId resolved = resolve(input);
        if (!live[resolved]) {
          live[resolved] = true;
          liveWorklist.push(resolved);
        }
      }
    }
  }
  while (!liveWorklist.empty()) {
    const OptimizerValueId value = liveWorklist.front();
    liveWorklist.pop();
    const OptimizerIrInstruction* definition = definitions[value];
    if (definition == nullptr) continue;
    for (const OptimizerValueId input : definition->inputs) {
      const OptimizerValueId resolved = resolve(input);
      if (!live[resolved]) {
        live[resolved] = true;
        liveWorklist.push(resolved);
      }
    }
  }
  for (OptimizerIrBlock& block : result.function.blocks) {
    for (auto instruction = block.instructions.begin(); instruction != block.instructions.end();) {
      if (producesValue(instruction->opcode) && !live[instruction->result]) {
        instruction = block.instructions.erase(instruction);
        ++result.stats.deadInstructionsRemoved;
      } else {
        ++instruction;
      }
    }
  }

  rewriteUses();
  const OptimizerIrVerificationResult optimized = verifyOptimizerIr(result.function);
  if (!optimized.valid) {
    result.error = optimized.error;
    return result;
  }
  result.valid = true;
  return result;
}

}  // namespace minijs
