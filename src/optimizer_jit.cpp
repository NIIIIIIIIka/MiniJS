#include "minijs/optimizer_jit.h"

#include <utility>
#include <cstdint>
#include <cstring>
#include <vector>

#include "minijs/bytecode_function.h"
#include "minijs/optimized_code.h"
#include "minijs/optimizer_deopt.h"
#include "minijs/optimizer_safepoint.h"

namespace minijs {
namespace {

bool emitWindowsX64Entry(OptimizedCode& code, std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  std::vector<std::uint8_t> bytes{
      0x53,                         // push rbx
      0x48, 0x83, 0xEC, 0x20,       // sub rsp, 32-byte shadow space
      0x48, 0x89, 0xCB,             // mov rbx, rcx (preserve frame)
      0x48, 0x89, 0xD9,             // mov rcx, rbx
      0x48, 0xB8,                   // mov rax, helper
  };
  const std::uint64_t helper = static_cast<std::uint64_t>(
      reinterpret_cast<std::uintptr_t>(&minijsExecuteOptimizedLowered));
  const std::size_t immediate = bytes.size();
  bytes.resize(bytes.size() + sizeof(helper));
  std::memcpy(bytes.data() + immediate, &helper, sizeof(helper));
  bytes.insert(bytes.end(), {
      0xFF, 0xD0,                   // call rax
      0x48, 0x83, 0xC4, 0x20,       // add rsp, 32
      0x5B,                         // pop rbx
      0xC3,                         // ret
  });
  code.executableMemory = ExecutableMemory::allocate(bytes, error);
  if (code.executableMemory == nullptr) return false;
  code.entry = reinterpret_cast<OptimizedEntry>(code.executableMemory->data());
  return true;
#else
  (void)code;
  error = "optimized native codegen currently requires Windows x64";
  return false;
#endif
}

void addEntrySafepoint(OptimizedCode& code, const BytecodeFunction& function) {
  OptimizedSafepoint safepoint;
  safepoint.id = 0;
  for (std::size_t index = 0; index < function.jit.optimize.parameterTypes.size(); ++index) {
    if (function.jit.optimize.parameterTypes[index] == ObservedType::Object) {
      safepoint.liveGcArguments.push_back(static_cast<std::uint32_t>(index));
    }
  }
  code.safepoints.push_back(std::move(safepoint));
  code.entrySafepointId = 0;
}

bool verifySafepoints(OptimizerCompileResult& result) {
  const OptimizerSafepointVerificationResult verified =
      verifyOptimizerSafepoints(*result.code);
  if (verified.valid) return true;
  result.error = verified.error;
  result.code.reset();
  return false;
}

OptimizerCompileResult compileMonomorphicProperty(const BytecodeFunction& function) {
  OptimizerCompileResult result;
  const BytecodeVerificationResult verified = verifyBytecode(function.chunk);
  if (!verified.valid) {
    result.error = verified.error;
    return result;
  }
  std::vector<DecodedInstruction> instructions;
  for (const std::size_t offset : verified.instructionOffsets) {
    instructions.push_back(decodeInstruction(function.chunk, offset));
  }

  OptimizedPropertyAccess access;
  std::size_t feedbackIndex = 0;
  const bool implicitReturnSuffix = instructions.size() >= 2 &&
      instructions[instructions.size() - 2].opcode == Opcode::Constant &&
      instructions.back().opcode == Opcode::Return;
  if ((instructions.size() == 3 || (instructions.size() == 5 && implicitReturnSuffix)) &&
      instructions[0].opcode == Opcode::GetLocal &&
      instructions[0].operands[0] == 0 &&
      instructions[1].opcode == Opcode::GetProperty &&
      instructions[2].opcode == Opcode::Return) {
    access.kind = OptimizedPropertyAccessKind::Load;
    access.objectArgument = 0;
    feedbackIndex = instructions[1].operands[1];
  } else if ((instructions.size() == 4 ||
              (instructions.size() == 6 && implicitReturnSuffix)) &&
             instructions[0].opcode == Opcode::GetLocal &&
             instructions[0].operands[0] == 0 &&
             instructions[1].opcode == Opcode::GetLocal &&
             instructions[1].operands[0] == 1 &&
             instructions[2].opcode == Opcode::SetProperty &&
             instructions[3].opcode == Opcode::Return) {
    access.kind = OptimizedPropertyAccessKind::Store;
    access.objectArgument = 0;
    access.valueArgument = 1;
    feedbackIndex = instructions[2].operands[1];
  } else {
    result.error = "optimizer object subset requires a direct property load or store return";
    for (const auto& instruction : instructions) {
      result.error += std::string(" ") + opcodeName(instruction.opcode);
    }
    return result;
  }
  if (function.params.empty() ||
      function.jit.optimize.parameterTypes[0] != ObservedType::Object) {
    result.error = "optimizer property receiver has no stable Object profile";
    return result;
  }
  const FeedbackSlot& feedback = function.chunk.feedbackSlot(feedbackIndex);
  if (feedback.state != FeedbackState::Monomorphic || feedback.property.size != 1 ||
      feedback.property.entries[0].shape == nullptr) {
    result.error = "optimizer property feedback is not monomorphic";
    return result;
  }
  access.shape = feedback.property.entries[0].shape;
  access.slot = feedback.property.entries[0].slot;
  access.deoptId = 0;

  result.code = std::make_shared<OptimizedCode>();
  result.code->parameterCount = function.params.size();
  result.code->propertyAccess = access;
  DeoptState state;
  state.id = 0;
  state.bytecodeOffset = access.kind == OptimizedPropertyAccessKind::Load
                             ? instructions[1].offset
                             : instructions[2].offset;
  state.phase = DeoptPhase::Before;
  state.stackDepth = function.params.size();
  for (std::size_t index = 0; index < function.params.size(); ++index) {
    DeoptValueLocation location;
    location.kind = DeoptLocationKind::Argument;
    location.index = static_cast<std::uint32_t>(index);
    state.frameSlots.push_back(location);
  }
  result.code->deoptStates.push_back(std::move(state));
  addEntrySafepoint(*result.code, function);
  const auto deopt = verifyOptimizerDeoptMetadata(*result.code);
  if (!deopt.valid) {
    result.error = deopt.error;
    result.code.reset();
    return result;
  }
  if (!verifySafepoints(result)) return result;
  if (!emitWindowsX64Entry(*result.code, result.error)) result.code.reset();
  return result;
}

OptimizerCompileResult compileMonomorphicMethod(const BytecodeFunction& function) {
  OptimizerCompileResult result;
  const BytecodeVerificationResult verified = verifyBytecode(function.chunk);
  if (!verified.valid) {
    result.error = verified.error;
    return result;
  }
  std::vector<DecodedInstruction> instructions;
  for (const std::size_t offset : verified.instructionOffsets) {
    instructions.push_back(decodeInstruction(function.chunk, offset));
  }

  std::size_t logicalSize = instructions.size();
  if (logicalSize >= 4 && instructions[logicalSize - 2].opcode == Opcode::Constant &&
      instructions[logicalSize - 1].opcode == Opcode::Return) {
    logicalSize -= 2;
  }
  if (logicalSize < 3 || instructions[0].opcode != Opcode::GetLocal ||
      instructions[0].operands[0] != 0 ||
      instructions[logicalSize - 2].opcode != Opcode::MethodCall ||
      instructions[logicalSize - 1].opcode != Opcode::Return) {
    result.error = "optimizer method subset requires a direct method call return";
    return result;
  }

  const DecodedInstruction& call = instructions[logicalSize - 2];
  const std::size_t argumentCount = call.operands[1];
  if (function.params.size() != argumentCount + 1 || logicalSize != argumentCount + 3) {
    result.error = "optimizer method call must forward all wrapper arguments";
    return result;
  }
  for (std::size_t index = 0; index < argumentCount; ++index) {
    const DecodedInstruction& argument = instructions[index + 1];
    if (argument.opcode != Opcode::GetLocal || argument.operands[0] != index + 1) {
      result.error = "optimizer method call arguments are not direct local forwards";
      return result;
    }
  }

  const FeedbackSlot& feedback = function.chunk.feedbackSlot(call.operands[2]);
  if (feedback.state != FeedbackState::Monomorphic || feedback.method.size != 1) {
    result.error = "optimizer method feedback is not monomorphic";
    return result;
  }
  const MethodInlineCacheEntry& cached = feedback.method.entries[0];
  if (cached.isStatic || cached.klass == nullptr || cached.method == nullptr) {
    result.error = "optimizer method feedback is not an instance method";
    return result;
  }

  result.code = std::make_shared<OptimizedCode>();
  result.code->parameterCount = function.params.size();
  result.code->methodCall.klass = cached.klass;
  result.code->methodCall.method = cached.method;
  result.code->methodCall.receiverArgument = 0;
  result.code->methodCall.deoptId = 0;
  for (std::size_t index = 0; index < argumentCount; ++index) {
    result.code->methodCall.argumentIndices.push_back(
        static_cast<std::uint32_t>(index + 1));
  }

  DeoptState state;
  state.id = 0;
  state.bytecodeOffset = call.offset;
  state.phase = DeoptPhase::Before;
  state.stackDepth = function.params.size();
  for (std::size_t index = 0; index < function.params.size(); ++index) {
    DeoptValueLocation location;
    location.kind = DeoptLocationKind::Argument;
    location.index = static_cast<std::uint32_t>(index);
    state.frameSlots.push_back(location);
  }
  result.code->deoptStates.push_back(std::move(state));
  addEntrySafepoint(*result.code, function);
  const auto deopt = verifyOptimizerDeoptMetadata(*result.code);
  if (!deopt.valid) {
    result.error = deopt.error;
    result.code.reset();
    return result;
  }
  if (!verifySafepoints(result)) return result;
  if (!emitWindowsX64Entry(*result.code, result.error)) result.code.reset();
  return result;
}

}  // namespace

OptimizerCompileResult compileOptimized(const BytecodeFunction& function) {
  OptimizerCompileResult result;
  const std::size_t parameterCount = function.params.size();
  if (function.jit.optimize.parameterTypes.size() != parameterCount) {
    result.error = "optimizer parameter profile is incomplete";
    return result;
  }

  std::vector<OptimizerType> parameterTypes;
  parameterTypes.reserve(parameterCount);
  bool hasObjectParameter = false;
  for (std::size_t index = 0; index < parameterCount; ++index) {
    const ObservedType observed = function.jit.optimize.parameterTypes[index];
    if (observed == ObservedType::Object) {
      hasObjectParameter = true;
      parameterTypes.push_back(OptimizerType::Object);
      continue;
    }
    if (observed != ObservedType::Number) {
      result.error = "optimizer parameter " + std::to_string(index) +
                     " does not have a supported stable profile";
      return result;
    }
    parameterTypes.push_back(OptimizerType::Number);
  }
  if (hasObjectParameter) {
    const BytecodeVerificationResult verified = verifyBytecode(function.chunk);
    if (!verified.valid) {
      result.error = verified.error;
      return result;
    }
    for (const std::size_t offset : verified.instructionOffsets) {
      if (decodeInstruction(function.chunk, offset).opcode == Opcode::MethodCall) {
        return compileMonomorphicMethod(function);
      }
    }
    return compileMonomorphicProperty(function);
  }

  const OptimizerIrResult built = buildOptimizerIr(function.chunk, parameterCount);
  if (!built.valid) {
    result.error = built.error;
    return result;
  }
  const OptimizerTypeAnalysisResult types =
      inferOptimizerTypes(built.function, function.chunk, parameterTypes);
  if (!types.valid) {
    result.error = types.error;
    return result;
  }
  const OptimizerEligibilityResult eligibility =
      analyzeOptimizerEligibility(built.function, function.chunk, types);
  if (!eligibility.eligible) {
    result.error = eligibility.error;
    return result;
  }
  const OptimizerGuardInsertionResult guarded =
      insertOptimizerNumberGuards(built.function, eligibility);
  if (!guarded.valid) {
    result.error = guarded.error;
    return result;
  }
  const OptimizerPassResult optimized = optimizeOptimizerIr(guarded.function, function.chunk);
  if (!optimized.valid) {
    result.error = optimized.error;
    return result;
  }
  OptimizerLoweringResult lowered =
      lowerOptimizerNumberIr(optimized.function, function.chunk, types);
  if (!lowered.valid) {
    result.error = lowered.error;
    return result;
  }

  result.code = std::make_shared<OptimizedCode>();
  result.code->parameterCount = parameterCount;
  result.code->ir = optimized.function;
  result.code->lowered = std::move(lowered.function);
  for (OptimizerLoweredBlock& block : result.code->lowered.blocks) {
    for (OptimizerLoweredInstruction& instruction : block.instructions) {
      if (instruction.opcode != OptimizerLoweredOpcode::GuardNumber) continue;
      DeoptState state;
      state.id = static_cast<OptimizerDeoptId>(result.code->deoptStates.size());
      state.bytecodeOffset = instruction.bytecodeOffset;
      state.phase = DeoptPhase::Before;
      state.stackDepth = parameterCount;
      for (std::size_t index = 0; index < parameterCount; ++index) {
        DeoptValueLocation location;
        location.kind = DeoptLocationKind::Argument;
        location.index = static_cast<std::uint32_t>(index);
        state.frameSlots.push_back(location);
      }
      instruction.deoptId = state.id;
      result.code->deoptStates.push_back(std::move(state));
    }
  }
  addEntrySafepoint(*result.code, function);
  const OptimizerDeoptVerificationResult deoptVerified =
      verifyOptimizerDeoptMetadata(*result.code);
  if (!deoptVerified.valid) {
    result.error = deoptVerified.error;
    result.code.reset();
    return result;
  }
  if (!verifySafepoints(result)) return result;
  if (!emitWindowsX64Entry(*result.code, result.error)) {
    result.code.reset();
  }
  return result;
}

}  // namespace minijs
