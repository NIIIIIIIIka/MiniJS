#include "minijs/optimizer_deopt.h"

#include <vector>

namespace minijs {

OptimizerDeoptVerificationResult verifyOptimizerDeoptMetadata(
    const OptimizedCode& code) {
  OptimizerDeoptVerificationResult result;
  auto fail = [&](std::string error) {
    result.error = std::move(error);
    return result;
  };

  std::vector<bool> referenced(code.deoptStates.size(), false);
  for (const OptimizerLoweredBlock& block : code.lowered.blocks) {
    for (const OptimizerLoweredInstruction& instruction : block.instructions) {
      if (instruction.opcode != OptimizerLoweredOpcode::GuardNumber) continue;
      if (instruction.deoptId >= code.deoptStates.size()) {
        return fail("optimizer guard has no deopt state");
      }
      if (referenced[instruction.deoptId]) {
        return fail("optimizer deopt id is referenced by multiple guards");
      }
      referenced[instruction.deoptId] = true;
      if (code.deoptStates[instruction.deoptId].bytecodeOffset !=
          instruction.bytecodeOffset) {
        return fail("optimizer guard and deopt bytecode offsets differ");
      }
    }
  }
  if (code.propertyAccess.kind != OptimizedPropertyAccessKind::None) {
    if (code.propertyAccess.deoptId >= code.deoptStates.size()) {
      return fail("optimizer property guard has no deopt state");
    }
    if (referenced[code.propertyAccess.deoptId]) {
      return fail("optimizer deopt id is referenced by multiple guards");
    }
    referenced[code.propertyAccess.deoptId] = true;
  }
  if (code.methodCall.valid()) {
    if (code.methodCall.deoptId >= code.deoptStates.size()) {
      return fail("optimizer method guard has no deopt state");
    }
    if (referenced[code.methodCall.deoptId]) {
      return fail("optimizer deopt id is referenced by multiple guards");
    }
    referenced[code.methodCall.deoptId] = true;
  }

  for (std::size_t index = 0; index < code.deoptStates.size(); ++index) {
    const DeoptState& state = code.deoptStates[index];
    if (state.id != index || !referenced[index]) {
      return fail("optimizer deopt state id is invalid or unreferenced");
    }
    if (state.stackDepth != state.frameSlots.size()) {
      return fail("optimizer deopt stack depth does not match frame slots");
    }
    for (const DeoptValueLocation& location : state.frameSlots) {
      if (location.kind == DeoptLocationKind::Argument &&
          location.index >= code.parameterCount) {
        return fail("optimizer deopt argument location is out of bounds");
      }
    }
  }
  result.valid = true;
  return result;
}

OptimizerDeoptMaterializationResult materializeOptimizerDeoptFrame(
    const OptimizedCode& code, OptimizerDeoptId deoptId,
    const std::vector<Value>& arguments) {
  OptimizerDeoptMaterializationResult result;
  const OptimizerDeoptVerificationResult verified = verifyOptimizerDeoptMetadata(code);
  if (!verified.valid) {
    result.error = verified.error;
    return result;
  }
  if (deoptId >= code.deoptStates.size()) {
    result.error = "optimizer deopt id is out of bounds";
    return result;
  }

  const DeoptState& state = code.deoptStates[deoptId];
  result.bytecodeOffset = state.bytecodeOffset;
  result.phase = state.phase;
  result.frameSlots.reserve(state.frameSlots.size());
  for (const DeoptValueLocation& location : state.frameSlots) {
    switch (location.kind) {
      case DeoptLocationKind::Argument:
        if (location.index >= arguments.size()) {
          result.error = "optimizer deopt argument is unavailable";
          return result;
        }
        result.frameSlots.push_back(arguments[location.index]);
        break;
      case DeoptLocationKind::NumberConstant:
        result.frameSlots.emplace_back(location.number);
        break;
      case DeoptLocationKind::BooleanConstant:
        result.frameSlots.emplace_back(location.boolean);
        break;
    }
  }
  result.valid = true;
  return result;
}

}  // namespace minijs
