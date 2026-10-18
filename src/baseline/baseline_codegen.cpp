#include "baseline_codegen.h"

#include "baseline_call_emitter.h"
#include "baseline_codegen_types.h"
#include "arm64/arm64_emitter.h"
#include "x64/windows_x64_emitter.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "minijs/baseline_runtime.h"
#include "minijs/bytecode_decoder.h"
#include "minijs/value.h"

namespace minijs {
namespace {

using detail::BinaryNumberGuardPatches;
using detail::Arm64Emitter;
using detail::BranchPatch;
using detail::emitRuntimeCall;
using detail::emitRuntimeCallWithThreeU32;
using detail::emitRuntimeCallWithTwoU32;
using detail::emitRuntimeCallWithU32;
using detail::NativeBranchKind;
using detail::NativeBranchPatch;
using detail::WindowsX64Emitter;

BaselineCompileResult failure(std::string error) {
  BaselineCompileResult result;
  result.error = std::move(error);
  return result;
}

}  // namespace

BaselineCompileResult detail::compileBaselineNative(const BytecodeFunction& function) {
  // native compiler 仍以 verifier + decoder 为前端，确保 codegen 只处理
  // 已确认边界和操作数合法的现有 bytecode Opcode。
  const BytecodeVerificationResult verification = verifyBytecode(function.chunk);
  if (!verification.valid) {
    return failure("bytecode verification failed: " + verification.error);
  }

  auto code = std::make_shared<BaselineCode>();
  code->bytecodeOffsetToInstructionIndex.assign(function.chunk.code().size() + 1,
                                                kInvalidInstructionIndex);
  code->bytecodeOffsetToNativeOffset.assign(function.chunk.code().size() + 1,
                                            kInvalidNativeOffset);
  code->instructions.reserve(verification.instructionOffsets.size());

  for (std::size_t index = 0; index < verification.instructionOffsets.size(); ++index) {
    const std::size_t bytecodeOffset = verification.instructionOffsets[index];
    code->bytecodeOffsetToInstructionIndex[bytecodeOffset] = index;
    code->instructions.push_back(decodeInstruction(function.chunk, bytecodeOffset));
  }

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  WindowsX64Emitter emitter;
#elif defined(__aarch64__) || defined(_M_ARM64)
  Arm64Emitter emitter;
#else
  return failure("baseline compiler has no native backend for this platform");
#endif
  std::vector<BranchPatch> epiloguePatches;
  std::vector<NativeBranchPatch> branchPatches;
  emitter.emitPrologue();

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  // 第二条若是独立跳转入口，不能执行第一条的压栈副作用。
  std::vector<bool> branchTargets(function.chunk.code().size() + 1, false);
  for (const auto& instruction : code->instructions) {
    if (instruction.opcode == Opcode::Jump || instruction.opcode == Opcode::JumpIfFalse ||
        instruction.opcode == Opcode::Loop) {
      branchTargets[instruction.jumpTarget] = true;
    }
  }
  const auto constantBinaryHelper = [&](std::size_t index) -> std::uintptr_t {
    if (index + 1 >= code->instructions.size()) return 0;
    const auto& first = code->instructions[index];
    const auto& second = code->instructions[index + 1];
    if (first.opcode != Opcode::Constant ||
        !function.chunk.constant(first.operands[0]).isNumber() ||
        branchTargets[verification.instructionOffsets[index + 1]]) return 0;
    switch (second.opcode) {
      case Opcode::Add: return reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd);
      case Opcode::Sub: return reinterpret_cast<std::uintptr_t>(&minijsBaselineSub);
      case Opcode::Mul: return reinterpret_cast<std::uintptr_t>(&minijsBaselineMul);
      case Opcode::Div:
        // +0/-0 的除零错误仍执行原始路径；NaN 不按零处理。
        return function.chunk.constant(first.operands[0]).asNumber() == 0.0 ? 0
            : reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv);
      case Opcode::Equal: return reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual);
      case Opcode::Greater: return reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater);
      case Opcode::Less: return reinterpret_cast<std::uintptr_t>(&minijsBaselineLess);
      default: return 0;
    }
  };
#endif

  // bytecodeOffsetToNativeOffset 记录每条 bytecode 指令对应的机器码起点；
  // 以后做调试、safepoint 或 deopt 时会从这里回到 bytecode 语义位置。
  for (std::size_t index = 0; index < code->instructions.size(); ++index) {
    const std::size_t bytecodeOffset = verification.instructionOffsets[index];
    const DecodedInstruction& instruction = code->instructions[index];
    code->bytecodeOffsetToNativeOffset[bytecodeOffset] = emitter.offset();

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (instruction.opcode == Opcode::GetLocal && index + 2 < code->instructions.size() &&
        code->instructions[index + 1].opcode == Opcode::JumpIfFalse &&
        code->instructions[index + 2].opcode == Opcode::Pop &&
        !branchTargets[verification.instructionOffsets[index + 1]] &&
        !branchTargets[verification.instructionOffsets[index + 2]]) {
      const auto falseOffset = code->instructions[index + 1].jumpTarget;
      const auto falseIndex = code->bytecodeOffsetToInstructionIndex[falseOffset];
      if (falseIndex < code->instructions.size() && falseIndex + 1 < code->instructions.size() &&
          code->instructions[falseIndex].opcode == Opcode::Pop) {
        std::string inlineError;
        if (!emitter.emitLocalTruthyOrRuntimeCall(instruction.operands[0], inlineError)) return failure(inlineError);
        // 原假分支先弹出条件副本；现在无副本，跳到该 Pop 的下一条指令。
        branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
            verification.instructionOffsets[falseIndex + 1], NativeBranchKind::Zero});
        epiloguePatches.push_back({emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
        for (std::size_t inner = 1; inner <= 2; ++inner) {
          code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
              code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        }
        index += 2;
        continue;
      }
    }
    if (instruction.opcode == Opcode::Constant && index + 1 < code->instructions.size() &&
        function.chunk.constant(instruction.operands[0]).isNumber() &&
        code->instructions[index + 1].opcode == Opcode::Return &&
        !branchTargets[verification.instructionOffsets[index + 1]]) {
      const double number = function.chunk.constant(instruction.operands[0]).asNumber();
      std::uint64_t bits = 0;
      static_assert(sizeof(number) == sizeof(bits), "double must have 64 bits");
      std::memcpy(&bits, &number, sizeof(bits));
      emitter.emitMoveFrameToFirstArg();
      emitter.emitMoveU64ToSecondArg(bits);
      emitter.emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineReturnNumberBits));
      emitter.emitCallHelper();
      epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
      epiloguePatches.push_back({emitter.emitJumpPlaceholder(), false});
      code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + 1]] =
          code->bytecodeOffsetToNativeOffset[bytecodeOffset];
      ++index;
      continue;
    }
    if (instruction.opcode == Opcode::GetLocal && index + 1 < code->instructions.size() &&
        code->instructions[index + 1].opcode == Opcode::Return &&
        !branchTargets[verification.instructionOffsets[index + 1]]) {
      emitRuntimeCallWithU32(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineReturnLocal),
                             instruction.operands[0]);
      // 成功已完成返回；失败已记录异常，两种情况都进入统一 epilogue。
      epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
      epiloguePatches.push_back({emitter.emitJumpPlaceholder(), false});
      code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + 1]] =
          code->bytecodeOffsetToNativeOffset[bytecodeOffset];
      ++index;
      continue;
    }
    if (instruction.opcode == Opcode::Constant && index + 1 < code->instructions.size() &&
        code->instructions[index + 1].opcode == Opcode::Return &&
        !branchTargets[verification.instructionOffsets[index + 1]]) {
      const auto& constant = function.chunk.constant(instruction.operands[0]);
      if (constant.isString() || constant.isBoolean() || constant.isNull() || constant.isUndefined()) {
        emitRuntimeCallWithU32(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineReturnConstant),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        epiloguePatches.push_back({emitter.emitJumpPlaceholder(), false});
        code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + 1]] =
            code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        ++index;
        continue;
      }
    }
    if (instruction.opcode == Opcode::Constant && index + 2 < code->instructions.size() &&
        (function.chunk.constant(instruction.operands[0]).isNumber() ||
         function.chunk.constant(instruction.operands[0]).isBoolean()) &&
        code->instructions[index + 1].opcode == Opcode::SetLocal &&
        code->instructions[index + 2].opcode == Opcode::Pop &&
        !branchTargets[verification.instructionOffsets[index + 1]] &&
        !branchTargets[verification.instructionOffsets[index + 2]]) {
      std::string inlineError;
      if (!emitter.emitScalarConstantAssignment(instruction.operands[0],
              code->instructions[index + 1].operands[0], function.chunk.constant(instruction.operands[0]),
              inlineError)) return failure(inlineError);
      epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
      for (std::size_t inner = 1; inner <= 2; ++inner) {
        code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
            code->bytecodeOffsetToNativeOffset[bytecodeOffset];
      }
      index += 2;
      continue;
    }
    if (instruction.opcode == Opcode::GetLocal && index + 2 < code->instructions.size() &&
        code->instructions[index + 1].opcode == Opcode::SetLocal &&
        code->instructions[index + 2].opcode == Opcode::Pop &&
        !branchTargets[verification.instructionOffsets[index + 1]] &&
        !branchTargets[verification.instructionOffsets[index + 2]]) {
      std::string inlineError;
      if (!emitter.emitLocalCopyOrRuntimeCall(instruction.operands[0],
              code->instructions[index + 1].operands[0], inlineError)) return failure(inlineError);
      epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
      for (std::size_t inner = 1; inner <= 2; ++inner) {
        code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
            code->bytecodeOffsetToNativeOffset[bytecodeOffset];
      }
      index += 2;
      continue;
    }
    if (instruction.opcode == Opcode::GetLocal && index + 3 < code->instructions.size()) {
      const auto operation = code->instructions[index + 1].opcode;
      bool independentEntry = false;
      for (std::size_t inner = 1; inner <= 3; ++inner) {
        independentEntry = independentEntry || branchTargets[verification.instructionOffsets[index + inner]];
      }
      if ((operation == Opcode::Negate || operation == Opcode::Not) &&
          code->instructions[index + 2].opcode == Opcode::SetLocal &&
          code->instructions[index + 3].opcode == Opcode::Pop && !independentEntry) {
        std::string inlineError;
        if (!emitter.emitLocalUnaryUpdateOrRuntimeCall(instruction.operands[0],
                code->instructions[index + 2].operands[0], operation, inlineError)) return failure(inlineError);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        for (std::size_t inner = 1; inner <= 3; ++inner) {
          code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
              code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        }
        index += 3;
        continue;
      }
    }
    if (instruction.opcode == Opcode::GetLocal && index + 4 < code->instructions.size()) {
      const auto& rightOperand = code->instructions[index + 1];
      const auto& arithmetic = code->instructions[index + 2];
      const auto& assign = code->instructions[index + 3];
      const bool isArithmetic = arithmetic.opcode == Opcode::Add || arithmetic.opcode == Opcode::Sub ||
                                arithmetic.opcode == Opcode::Mul || arithmetic.opcode == Opcode::Div;
      const bool isComparison = arithmetic.opcode == Opcode::Equal || arithmetic.opcode == Opcode::Less ||
                                arithmetic.opcode == Opcode::Greater;
      bool independentEntry = false;
      for (std::size_t inner = 1; inner <= 4; ++inner) {
        independentEntry = independentEntry || branchTargets[verification.instructionOffsets[index + inner]];
      }
      if (rightOperand.opcode == Opcode::Constant && arithmetic.opcode == Opcode::Equal &&
          (function.chunk.constant(rightOperand.operands[0]).isBoolean() ||
           function.chunk.constant(rightOperand.operands[0]).isNull() ||
           function.chunk.constant(rightOperand.operands[0]).isUndefined()) &&
          assign.opcode == Opcode::SetLocal && code->instructions[index + 4].opcode == Opcode::Pop &&
          !independentEntry) {
        std::string inlineError;
        if (!emitter.emitScalarConstantComparisonAssignment(instruction.operands[0], rightOperand.operands[0],
                function.chunk.constant(rightOperand.operands[0]), assign.operands[0], inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        for (std::size_t inner = 1; inner <= 4; ++inner) {
          code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
              code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        }
        index += 4;
        continue;
      }
      std::uintptr_t updateHelper = isArithmetic || isComparison ? constantBinaryHelper(index + 1) : 0;
      if ((isArithmetic || isComparison) && rightOperand.opcode == Opcode::GetLocal) {
        updateHelper = arithmetic.opcode == Opcode::Add ? reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd)
                     : arithmetic.opcode == Opcode::Sub ? reinterpret_cast<std::uintptr_t>(&minijsBaselineSub)
                     : arithmetic.opcode == Opcode::Mul ? reinterpret_cast<std::uintptr_t>(&minijsBaselineMul)
                     : arithmetic.opcode == Opcode::Div ? reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv)
                     : arithmetic.opcode == Opcode::Equal ? reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual)
                     : arithmetic.opcode == Opcode::Less ? reinterpret_cast<std::uintptr_t>(&minijsBaselineLess)
                                                         : reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater);
      }
      if (updateHelper != 0 &&
          assign.opcode == Opcode::SetLocal &&
          code->instructions[index + 4].opcode == Opcode::Pop && !independentEntry) {
        std::string inlineError;
        if (isComparison) {
          const bool constantRight = rightOperand.opcode == Opcode::Constant;
          std::uint64_t bits = 0;
          if (constantRight) {
            const double number = function.chunk.constant(rightOperand.operands[0]).asNumber();
            std::memcpy(&bits, &number, sizeof(bits));
          }
          if (!emitter.emitComparisonAssignment(instruction.operands[0], rightOperand.operands[0],
                  constantRight, bits, assign.operands[0], arithmetic.opcode, updateHelper, inlineError)) {
            return failure(inlineError);
          }
        } else if (rightOperand.opcode == Opcode::GetLocal) {
          if (!emitter.emitNumberLocalPairUpdateOrRuntimeCall(
                  instruction.operands[0], rightOperand.operands[0], assign.operands[0],
                  arithmetic.opcode, updateHelper, inlineError)) {
            return failure(inlineError);
          }
        } else {
          const double number = function.chunk.constant(rightOperand.operands[0]).asNumber();
          std::uint64_t bits = 0;
          std::memcpy(&bits, &number, sizeof(bits));
          if (!emitter.emitNumberLocalUpdateOrRuntimeCall(
                  instruction.operands[0], bits, assign.operands[0], arithmetic.opcode, updateHelper, inlineError)) {
            return failure(inlineError);
          }
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        for (std::size_t inner = 1; inner <= 4; ++inner) {
          code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
              code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        }
        index += 4;
        continue;
      }
    }
    if (instruction.opcode == Opcode::GetLocal && index + 4 < code->instructions.size() &&
        code->instructions[index + 1].opcode == Opcode::Constant &&
        (function.chunk.constant(code->instructions[index + 1].operands[0]).isNumber() ||
         ((function.chunk.constant(code->instructions[index + 1].operands[0]).isBoolean() ||
           function.chunk.constant(code->instructions[index + 1].operands[0]).isNull() ||
           function.chunk.constant(code->instructions[index + 1].operands[0]).isUndefined()) &&
          code->instructions[index + 2].opcode == Opcode::Equal)) &&
        (code->instructions[index + 2].opcode == Opcode::Less ||
         code->instructions[index + 2].opcode == Opcode::Greater ||
         code->instructions[index + 2].opcode == Opcode::Equal) &&
        code->instructions[index + 3].opcode == Opcode::JumpIfFalse &&
        code->instructions[index + 4].opcode == Opcode::Pop) {
      bool independentEntry = false;
      for (std::size_t inner = 1; inner <= 4; ++inner) {
        independentEntry = independentEntry || branchTargets[verification.instructionOffsets[index + inner]];
      }
      const auto falseIndex = code->bytecodeOffsetToInstructionIndex[code->instructions[index + 3].jumpTarget];
      if (!independentEntry && falseIndex < code->instructions.size() &&
          falseIndex + 1 < code->instructions.size() && code->instructions[falseIndex].opcode == Opcode::Pop) {
        const auto constantIndex = code->instructions[index + 1].operands[0];
        const auto& constant = function.chunk.constant(constantIndex);
        std::string inlineError;
        if (constant.isNull() || constant.isUndefined()) {
          if (!emitter.emitEmptyConstantComparisonBranch(instruction.operands[0], constantIndex,
                  constant.isNull() ? Value::nullTypeTag() : Value::undefinedTypeTag(), inlineError)) {
            return failure(inlineError);
          }
        } else if (constant.isBoolean()) {
          // 编译期已确认 Boolean；isTruthy 这里只提取 0/1，不转换其它 Value。
          if (!emitter.emitBooleanConstantComparisonBranch(instruction.operands[0], constantIndex,
                  constant.isTruthy(), inlineError)) return failure(inlineError);
        } else {
          const double number = constant.asNumber();
          std::uint64_t bits = 0;
          std::memcpy(&bits, &number, sizeof(bits));
          if (!emitter.emitComparisonBranch(instruction.operands[0], constantIndex,
                  code->instructions[index + 2].opcode, true, bits, inlineError)) return failure(inlineError);
        }
        branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
            verification.instructionOffsets[falseIndex + 1], NativeBranchKind::Zero});
        epiloguePatches.push_back({emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
        for (std::size_t inner = 1; inner <= 4; ++inner) {
          code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
              code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        }
        index += 4;
        continue;
      }
    }
    if (instruction.opcode == Opcode::GetLocal && index + 2 < code->instructions.size() &&
        code->instructions[index + 1].opcode == Opcode::Constant &&
        (function.chunk.constant(code->instructions[index + 1].operands[0]).isBoolean() ||
         function.chunk.constant(code->instructions[index + 1].operands[0]).isNull() ||
         function.chunk.constant(code->instructions[index + 1].operands[0]).isUndefined()) &&
        code->instructions[index + 2].opcode == Opcode::Equal &&
        !branchTargets[verification.instructionOffsets[index + 1]] &&
        !branchTargets[verification.instructionOffsets[index + 2]]) {
      std::string inlineError;
      const auto constantIndex = code->instructions[index + 1].operands[0];
      const auto& constant = function.chunk.constant(constantIndex);
      if (constant.isBoolean()) {
        if (!emitter.emitBooleanConstantComparisonExpression(instruction.operands[0], constantIndex,
                constant.isTruthy(), inlineError)) return failure(inlineError);
      } else {
        if (!emitter.emitEmptyConstantComparisonExpression(instruction.operands[0], constantIndex,
                constant.isNull() ? Value::nullTypeTag() : Value::undefinedTypeTag(), inlineError)) return failure(inlineError);
      }
      epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
      for (std::size_t inner = 1; inner <= 2; ++inner) {
        code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
            code->bytecodeOffsetToNativeOffset[bytecodeOffset];
      }
      index += 2;
      continue;
    }
    if (instruction.opcode == Opcode::GetLocal && index + 2 < code->instructions.size() &&
        code->instructions[index + 1].opcode == Opcode::GetLocal &&
        !branchTargets[verification.instructionOffsets[index + 1]] &&
        !branchTargets[verification.instructionOffsets[index + 2]]) {
      const Opcode operation = code->instructions[index + 2].opcode;
      std::uintptr_t helper = 0;
      switch (operation) {
        case Opcode::Add: helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd); break;
        case Opcode::Sub: helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineSub); break;
        case Opcode::Mul: helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineMul); break;
        case Opcode::Div: helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv); break;
        case Opcode::Equal: helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual); break;
        case Opcode::Greater: helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater); break;
        case Opcode::Less: helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineLess); break;
        default: break;
      }
      if (helper != 0) {
        std::string inlineError;
        const bool comparison = operation == Opcode::Equal || operation == Opcode::Greater || operation == Opcode::Less;
        if (comparison && index + 4 < code->instructions.size() &&
            code->instructions[index + 3].opcode == Opcode::JumpIfFalse &&
            code->instructions[index + 4].opcode == Opcode::Pop &&
            !branchTargets[verification.instructionOffsets[index + 3]] &&
            !branchTargets[verification.instructionOffsets[index + 4]]) {
          const auto falseIndex = code->bytecodeOffsetToInstructionIndex[code->instructions[index + 3].jumpTarget];
          if (falseIndex < code->instructions.size() && falseIndex + 1 < code->instructions.size() &&
              code->instructions[falseIndex].opcode == Opcode::Pop) {
            if (!emitter.emitComparisonBranch(instruction.operands[0],
                    code->instructions[index + 1].operands[0], operation, false, 0, inlineError)) return failure(inlineError);
            branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                verification.instructionOffsets[falseIndex + 1], NativeBranchKind::Zero});
            epiloguePatches.push_back({emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
            for (std::size_t inner = 1; inner <= 4; ++inner) {
              code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
                  code->bytecodeOffsetToNativeOffset[bytecodeOffset];
            }
            index += 4;
            continue;
          }
        }
        if (!emitter.emitLocalPairExpressionOrRuntimeCall(
                instruction.operands[0], code->instructions[index + 1].operands[0], operation, helper, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        const bool foldBranch = comparison && index + 3 < code->instructions.size() &&
            code->instructions[index + 3].opcode == Opcode::JumpIfFalse &&
            !branchTargets[verification.instructionOffsets[index + 3]];
        if (foldBranch) {
          emitter.emitLoadBooleanTopResult();
          branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                   code->instructions[index + 3].jumpTarget, NativeBranchKind::Zero});
        }
        const std::size_t last = foldBranch ? 3 : 2;
        for (std::size_t inner = 1; inner <= last; ++inner) {
          code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
              code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        }
        index += last;
        continue;
      }
    }
    const std::uintptr_t binaryHelper = constantBinaryHelper(index);
    if (binaryHelper != 0) {
      const Opcode operation = code->instructions[index + 1].opcode;
      const bool foldBranch = (operation == Opcode::Equal || operation == Opcode::Greater ||
                               operation == Opcode::Less) && index + 2 < code->instructions.size() &&
          code->instructions[index + 2].opcode == Opcode::JumpIfFalse &&
          !branchTargets[verification.instructionOffsets[index + 2]];
      const double number = function.chunk.constant(instruction.operands[0]).asNumber();
      std::uint64_t bits = 0;
      std::memcpy(&bits, &number, sizeof(bits));
      std::string inlineError;
      if (!emitter.emitNumberConstantBinaryOrRuntimeCall(
              bits, operation, binaryHelper, inlineError, foldBranch)) {
        return failure(inlineError);
      }
      if (foldBranch) {
        branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                 code->instructions[index + 2].jumpTarget, NativeBranchKind::Zero});
        epiloguePatches.push_back({emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
        code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + 2]] =
            code->bytecodeOffsetToNativeOffset[bytecodeOffset];
      } else {
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
      }
      code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + 1]] =
          code->bytecodeOffsetToNativeOffset[bytecodeOffset];
      index += foldBranch ? 2 : 1;
      continue;
    }
    if (instruction.opcode == Opcode::GetLocal && index + 1 < code->instructions.size()) {
      const auto& next = code->instructions[index + 1];
      const std::size_t nextOffset = verification.instructionOffsets[index + 1];
      if (next.opcode == Opcode::Constant && !branchTargets[nextOffset] &&
          function.chunk.constant(next.operands[0]).isNumber() &&
          constantBinaryHelper(index + 1) == 0) {
        const double number = function.chunk.constant(next.operands[0]).asNumber();
        std::uint64_t bits = 0;
        std::memcpy(&bits, &number, sizeof(bits));
        std::string inlineError;
        if (!emitter.emitNumberConstant(
                bits, reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocalAndPushNumberSlot),
                inlineError, static_cast<int>(instruction.operands[0]))) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        // 两条 bytecode 保留原编号；native 映射指向同一合并单元，不是 deopt 点。
        code->bytecodeOffsetToNativeOffset[nextOffset] =
            code->bytecodeOffsetToNativeOffset[bytecodeOffset];
        ++index;
        continue;
      }
    }
#endif

    switch (instruction.opcode) {
      case Opcode::Constant:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
        if (function.chunk.constant(instruction.operands[0]).isNumber()) {
          const double number = function.chunk.constant(instruction.operands[0]).asNumber();
          std::uint64_t numberBits = 0;
          static_assert(sizeof(numberBits) == sizeof(number), "double must have 64 bits");
          // 复制位编码，保留负零和 NaN payload，并避免违反严格别名规则。
          std::memcpy(&numberBits, &number, sizeof(numberBits));
          std::string inlineError;
          if (!emitter.emitNumberConstant(
                  numberBits, reinterpret_cast<std::uintptr_t>(&minijsBaselinePushNumberSlot),
                  inlineError)) {
            return failure(inlineError);
          }
          epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
          break;
        }
#endif
        emitter.emitMoveFrameToFirstArg();
        emitter.emitMoveU32ToSecondArg(instruction.operands[0]);
        emitter.emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselinePushConstant));
        emitter.emitCallHelper();
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::GetLocal:
      case Opcode::SetLocal:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        const bool read = instruction.opcode == Opcode::GetLocal;
        std::string inlineError;
        if (!emitter.emitNumberLocalOrRuntimeCall(
                instruction.operands[0], read,
                read ? reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal)
                     : reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal),
                reinterpret_cast<std::uintptr_t>(&minijsBaselinePushNumberSlot), inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
      }
#else
        emitRuntimeCallWithU32(emitter,
                               instruction.opcode == Opcode::GetLocal
                                   ? reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal)
                                   : reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Pop:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselinePop));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::DefineGlobal:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineDefineGlobal),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::GetGlobal:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineGetGlobal),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::SetGlobal:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineSetGlobal),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Array:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineArray),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::GetIndex:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineGetIndex));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::SetIndex:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineSetIndex));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Object:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineObject),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::GetProperty:
        emitRuntimeCallWithTwoU32(
            emitter,
            reinterpret_cast<std::uintptr_t>(&minijsBaselineGetProperty),
            instruction.operands[0],
            instruction.operands[1]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::SetProperty:
        emitRuntimeCallWithTwoU32(
            emitter,
            reinterpret_cast<std::uintptr_t>(&minijsBaselineSetProperty),
            instruction.operands[0],
            instruction.operands[1]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Class:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineClass),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Method:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineMethod),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::StaticMethod:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineStaticMethod),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Inherit:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineInherit));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::GetUpvalue:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineGetUpvalue),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::SetUpvalue:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineSetUpvalue),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::CloseUpvalue:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineCloseUpvalue));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::GetCurrentClosure:
        emitRuntimeCall(emitter,
                        reinterpret_cast<std::uintptr_t>(&minijsBaselineGetCurrentClosure));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Closure:
        emitRuntimeCallWithTwoU32(
            emitter,
            reinterpret_cast<std::uintptr_t>(&minijsBaselineClosure),
            instruction.operands[0],
            static_cast<std::uint32_t>(instruction.offset + 2));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Call:
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineCall),
                               instruction.operands[0]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::MethodCall:
        emitRuntimeCallWithThreeU32(
            emitter,
            reinterpret_cast<std::uintptr_t>(&minijsBaselineMethodCall),
            instruction.operands[0],
            instruction.operands[1],
            instruction.operands[2]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::SuperCall:
        emitRuntimeCallWithTwoU32(
            emitter,
            reinterpret_cast<std::uintptr_t>(&minijsBaselineSuperCall),
            instruction.operands[0],
            instruction.operands[1]);
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Add:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitInlineNumberBinaryOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd),
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize),
                0x58, "add", false, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Sub:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitInlineNumberBinaryOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSub),
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize),
                0x5C, "sub", false, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineSub));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Mul:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitInlineNumberBinaryOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineMul),
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize),
                0x59, "mul", false, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineMul));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Div:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitInlineNumberBinaryOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv),
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize),
                0x5E, "div", true, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Mod:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitGuardedNumberBinaryRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineModNumber),
                reinterpret_cast<std::uintptr_t>(&minijsBaselineMod),
                "mod", true, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineMod));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Negate:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitInlineNumberNegateOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineNegate), inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineNegate));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Equal:
      case Opcode::Greater:
      case Opcode::Less:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        const auto helper = instruction.opcode == Opcode::Equal ? &minijsBaselineEqual
                            : instruction.opcode == Opcode::Greater ? &minijsBaselineGreater
                                                                    : &minijsBaselineLess;
        if (!emitter.emitInlineNumberComparisonOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(helper),
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize),
                instruction.opcode, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        if (index + 1 < code->instructions.size() &&
            code->instructions[index + 1].opcode == Opcode::JumpIfFalse &&
            !branchTargets[verification.instructionOffsets[index + 1]]) {
          emitter.emitLoadBooleanTopResult();
          branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                   code->instructions[index + 1].jumpTarget, NativeBranchKind::Zero});
          code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + 1]] =
              code->bytecodeOffsetToNativeOffset[bytecodeOffset];
          ++index;
        }
        break;
      }
#else
        emitRuntimeCall(emitter, instruction.opcode == Opcode::Equal
                                     ? reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual)
                                 : instruction.opcode == Opcode::Greater
                                     ? reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater)
                                     : reinterpret_cast<std::uintptr_t>(&minijsBaselineLess));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;
#endif

      case Opcode::Not:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitBooleanNotOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineNot), inlineError)) {
          return failure(inlineError);
        }
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineNot));
#endif
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::JumpIfFalse:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitBooleanTruthyOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselinePeekTruthy), inlineError)) {
          return failure(inlineError);
        }
      }
#else
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselinePeekTruthy));
#endif
        branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(), instruction.jumpTarget,
                                 NativeBranchKind::Zero});
        epiloguePatches.push_back({emitter.emitJumpIfNotOnePlaceholder(),
                                   NativeBranchKind::NotOne});
        break;

      case Opcode::Jump:
        branchPatches.push_back({emitter.emitJumpPlaceholder(), instruction.jumpTarget,
                                 NativeBranchKind::Unconditional});
        break;

      case Opcode::Loop:
        emitRuntimeCall(emitter,
                        reinterpret_cast<std::uintptr_t>(&minijsBaselineRecordLoopBackedge));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        branchPatches.push_back({emitter.emitJumpPlaceholder(), instruction.jumpTarget,
                                 NativeBranchKind::Unconditional});
        break;

      case Opcode::Return:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineReturn));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        epiloguePatches.push_back({emitter.emitJumpPlaceholder(), false});
        break;

      default:
        return failure(std::string("baseline compiler does not support ") +
                       opcodeName(instruction.opcode));
    }

  }

  const std::size_t epilogueOffset = emitter.offset();
  code->bytecodeOffsetToNativeOffset[function.chunk.code().size()] = epilogueOffset;
  emitter.emitEpilogue();

  for (const BranchPatch& patch : epiloguePatches) {
    std::string patchError;
    if (!emitter.patchBranchTo(patch.offset, epilogueOffset, patch.kind, patchError)) {
      return failure(patchError);
    }
  }

  for (const NativeBranchPatch& patch : branchPatches) {
    if (patch.bytecodeTarget >= code->bytecodeOffsetToNativeOffset.size()) {
      return failure("baseline branch target out of range");
    }
    const std::size_t nativeTarget = code->bytecodeOffsetToNativeOffset[patch.bytecodeTarget];
    if (nativeTarget == kInvalidNativeOffset) {
      return failure("baseline branch target has no native offset");
    }

    std::string patchError;
    if (!emitter.patchBranchTo(patch.offset, nativeTarget, patch.kind, patchError)) {
      return failure(patchError);
    }
  }

  std::string memoryError;
  code->executableMemory = ExecutableMemory::allocate(emitter.bytes(), memoryError);
  if (code->executableMemory == nullptr) {
    return failure("executable memory allocation failed: " + memoryError);
  }
  code->entry = reinterpret_cast<BaselineEntry>(code->executableMemory->data());

  BaselineCompileResult result;
  result.code = std::move(code);
  return result;
}

}  // namespace minijs
