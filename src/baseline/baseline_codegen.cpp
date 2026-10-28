#include "baseline_codegen.h"

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

#include "arm64/arm64_emitter.h"
#include "baseline_call_emitter.h"
#include "baseline_codegen_types.h"
#include "minijs/baseline_runtime.h"
#include "minijs/bytecode_decoder.h"
#include "minijs/value.h"
#include "x64/windows_x64_emitter.h"

namespace minijs {
namespace {

using detail::Arm64Emitter;
using detail::BinaryOperation;
using detail::BinaryNumberGuardPatches;
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

template <typename Emitter>
class BaselineBytecodeVisitor {
 public:
  BaselineBytecodeVisitor(const BytecodeFunction& function, std::shared_ptr<BaselineCode>& code,
                          const BytecodeVerificationResult& verification, Emitter& emitter,
                          std::vector<BranchPatch>& epiloguePatches,
                          std::vector<NativeBranchPatch>& branchPatches, std::size_t& index,
                          std::size_t bytecodeOffset
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
                          ,
                          const std::vector<bool>& branchTargets
#endif
                          )
      : function_(function),
        code_(code),
        verification_(verification),
        emitter_(emitter),
        epiloguePatches_(epiloguePatches),
        branchPatches_(branchPatches),
        index_(index),
        bytecodeOffset_(bytecodeOffset)
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
        ,
        branchTargets_(branchTargets)
#endif
  {
  }

  bool Visit(const DecodedInstruction& instruction, std::string& error) {
    instruction_ = &instruction;
    switch (instruction.opcode) {
      case Opcode::Constant:
        return VisitConstant(error);
      case Opcode::GetLocal:
        return VisitGetLocal(error);
      case Opcode::SetLocal:
        return VisitSetLocal(error);
      case Opcode::Pop:
        return VisitPop();
      case Opcode::DefineGlobal:
        return VisitDefineGlobal();
      case Opcode::GetGlobal:
        return VisitGetGlobal();
      case Opcode::SetGlobal:
        return VisitSetGlobal();
      case Opcode::Array:
        return VisitArray();
      case Opcode::GetIndex:
        return VisitGetIndex();
      case Opcode::SetIndex:
        return VisitSetIndex();
      case Opcode::Object:
        return VisitObject();
      case Opcode::GetProperty:
        return VisitGetProperty();
      case Opcode::SetProperty:
        return VisitSetProperty();
      case Opcode::Class:
        return VisitClass();
      case Opcode::Method:
        return VisitMethod();
      case Opcode::StaticMethod:
        return VisitStaticMethod();
      case Opcode::Inherit:
        return VisitInherit();
      case Opcode::GetUpvalue:
        return VisitGetUpvalue();
      case Opcode::SetUpvalue:
        return VisitSetUpvalue();
      case Opcode::CloseUpvalue:
        return VisitCloseUpvalue();
      case Opcode::GetCurrentClosure:
        return VisitGetCurrentClosure();
      case Opcode::Closure:
        return VisitClosure();
      case Opcode::Call:
        return VisitCall();
      case Opcode::MethodCall:
        return VisitMethodCall();
      case Opcode::SuperCall:
        return VisitSuperCall();
      case Opcode::Add:
        return VisitAdd(error);
      case Opcode::Sub:
        return VisitSub(error);
      case Opcode::Mul:
        return VisitMul(error);
      case Opcode::Div:
        return VisitDiv(error);
      case Opcode::Mod:
        return VisitMod(error);
      case Opcode::Negate:
        return VisitNegate(error);
      case Opcode::Equal:
        return VisitEqual(error);
      case Opcode::Greater:
        return VisitGreater(error);
      case Opcode::Less:
        return VisitLess(error);
      case Opcode::Not:
        return VisitNot(error);
      case Opcode::JumpIfFalse:
        return VisitJumpIfFalse(error);
      case Opcode::Jump:
        return VisitJump();
      case Opcode::Loop:
        return VisitLoop();
      case Opcode::Return:
        return VisitReturn();
      default:
        error = std::string("baseline compiler does not support ") + opcodeName(instruction.opcode);
        return false;
    }
  }

 private:
  void RecordFallibleCall() {
    epiloguePatches_.push_back({emitter_.emitJumpIfFalsePlaceholder(), true});
  }

  bool VisitRuntimeCall(std::uintptr_t helper) {
    emitRuntimeCall(emitter_, helper);
    RecordFallibleCall();
    return true;
  }

  bool VisitRuntimeCallWithU32(std::uintptr_t helper, std::uint32_t operand) {
    emitRuntimeCallWithU32(emitter_, helper, operand);
    RecordFallibleCall();
    return true;
  }

  bool VisitConstant(std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    const Value& constant = function_.chunk.constant(instruction_->operands[0]);
    if (constant.isNumber()) {
      const double number = constant.asNumber();
      std::uint64_t numberBits = 0;
      static_assert(sizeof(numberBits) == sizeof(number), "double must have 64 bits");
      // 复制位编码，保留负零和 NaN payload，并避免违反严格别名规则。
      std::memcpy(&numberBits, &number, sizeof(numberBits));
      if (!emitter_.emitNumberConstant(
              numberBits, reinterpret_cast<std::uintptr_t>(&minijsBaselinePushNumberSlot), error)) {
        return false;
      }
      RecordFallibleCall();
      return true;
    }
#endif
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselinePushConstant),
                                   instruction_->operands[0]);
  }

  bool VisitLocal(bool read, std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!emitter_.emitNumberLocalOrRuntimeCall(
            instruction_->operands[0], read,
            read ? reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal)
                 : reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal),
            reinterpret_cast<std::uintptr_t>(&minijsBaselinePushNumberSlot), error)) {
      return false;
    }
    RecordFallibleCall();
    return true;
#else
    return VisitRuntimeCallWithU32(read ? reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal)
                                        : reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal),
                                   instruction_->operands[0]);
#endif
  }

  bool VisitGetLocal(std::string& error) { return VisitLocal(true, error); }
  bool VisitSetLocal(std::string& error) { return VisitLocal(false, error); }
  bool VisitPop() { return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselinePop)); }
  bool VisitDefineGlobal() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineDefineGlobal),
                                   instruction_->operands[0]);
  }
  bool VisitGetGlobal() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetGlobal),
                                   instruction_->operands[0]);
  }
  bool VisitSetGlobal() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineSetGlobal),
                                   instruction_->operands[0]);
  }
  bool VisitArray() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineArray),
                                   instruction_->operands[0]);
  }
  bool VisitGetIndex() {
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetIndex));
  }
  bool VisitSetIndex() {
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineSetIndex));
  }
  bool VisitObject() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineObject),
                                   instruction_->operands[0]);
  }

  bool VisitGetProperty() {
    emitRuntimeCallWithTwoU32(emitter_,
                              reinterpret_cast<std::uintptr_t>(&minijsBaselineGetProperty),
                              instruction_->operands[0], instruction_->operands[1]);
    RecordFallibleCall();
    return true;
  }

  bool VisitSetProperty() {
    emitRuntimeCallWithTwoU32(emitter_,
                              reinterpret_cast<std::uintptr_t>(&minijsBaselineSetProperty),
                              instruction_->operands[0], instruction_->operands[1]);
    RecordFallibleCall();
    return true;
  }

  bool VisitClass() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineClass),
                                   instruction_->operands[0]);
  }
  bool VisitMethod() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineMethod),
                                   instruction_->operands[0]);
  }
  bool VisitStaticMethod() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineStaticMethod),
                                   instruction_->operands[0]);
  }
  bool VisitInherit() {
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineInherit));
  }
  bool VisitGetUpvalue() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetUpvalue),
                                   instruction_->operands[0]);
  }
  bool VisitSetUpvalue() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineSetUpvalue),
                                   instruction_->operands[0]);
  }
  bool VisitCloseUpvalue() {
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineCloseUpvalue));
  }
  bool VisitGetCurrentClosure() {
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetCurrentClosure));
  }

  bool VisitClosure() {
    emitRuntimeCallWithTwoU32(emitter_, reinterpret_cast<std::uintptr_t>(&minijsBaselineClosure),
                              instruction_->operands[0],
                              static_cast<std::uint32_t>(instruction_->offset + 2));
    RecordFallibleCall();
    return true;
  }

  bool VisitCall() {
    return VisitRuntimeCallWithU32(reinterpret_cast<std::uintptr_t>(&minijsBaselineCall),
                                   instruction_->operands[0]);
  }

  bool VisitMethodCall() {
    emitRuntimeCallWithThreeU32(
        emitter_, reinterpret_cast<std::uintptr_t>(&minijsBaselineMethodCall),
        instruction_->operands[0], instruction_->operands[1], instruction_->operands[2]);
    RecordFallibleCall();
    return true;
  }

  bool VisitSuperCall() {
    emitRuntimeCallWithTwoU32(emitter_, reinterpret_cast<std::uintptr_t>(&minijsBaselineSuperCall),
                              instruction_->operands[0], instruction_->operands[1]);
    RecordFallibleCall();
    return true;
  }

  bool VisitNumberBinary(BinaryOperation operation, std::uintptr_t helper,
                         std::string_view operationName, bool rejectZeroRight,
                         std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!emitter_.emitInlineNumberBinaryOrRuntimeCall(
            operation, helper, reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize),
            operationName, rejectZeroRight, error)) {
      return false;
    }
    RecordFallibleCall();
    return true;
#else
    return VisitRuntimeCall(helper);
#endif
  }

  bool VisitAdd(std::string& error) {
    return VisitNumberBinary(BinaryOperation::Add,
                             reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd), "add", false,
                             error);
  }
  bool VisitSub(std::string& error) {
    return VisitNumberBinary(BinaryOperation::Subtract,
                             reinterpret_cast<std::uintptr_t>(&minijsBaselineSub), "sub", false,
                             error);
  }
  bool VisitMul(std::string& error) {
    return VisitNumberBinary(BinaryOperation::Multiply,
                             reinterpret_cast<std::uintptr_t>(&minijsBaselineMul), "mul", false,
                             error);
  }
  bool VisitDiv(std::string& error) {
    return VisitNumberBinary(BinaryOperation::Divide,
                             reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv), "div", true,
                             error);
  }

  bool VisitMod(std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!emitter_.emitGuardedNumberBinaryRuntimeCall(
            reinterpret_cast<std::uintptr_t>(&minijsBaselineModNumber),
            reinterpret_cast<std::uintptr_t>(&minijsBaselineMod), "mod", true, error)) {
      return false;
    }
    RecordFallibleCall();
    return true;
#else
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineMod));
#endif
  }

  bool VisitNegate(std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!emitter_.emitInlineNumberNegateOrRuntimeCall(
            reinterpret_cast<std::uintptr_t>(&minijsBaselineNegate), error)) {
      return false;
    }
    RecordFallibleCall();
    return true;
#else
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineNegate));
#endif
  }

  bool VisitComparison(std::string& error) {
    const Opcode opcode = instruction_->opcode;
    const std::uintptr_t helper =
        opcode == Opcode::Equal     ? reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual)
        : opcode == Opcode::Greater ? reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater)
                                    : reinterpret_cast<std::uintptr_t>(&minijsBaselineLess);
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!emitter_.emitInlineNumberComparisonOrRuntimeCall(
            helper, reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize), opcode,
            error)) {
      return false;
    }
    RecordFallibleCall();
    if (index_ + 1 < code_->instructions.size() &&
        code_->instructions[index_ + 1].opcode == Opcode::JumpIfFalse &&
        !branchTargets_[verification_.instructionOffsets[index_ + 1]]) {
      emitter_.emitLoadBooleanTopResult();
      branchPatches_.push_back({emitter_.emitJumpIfZeroPlaceholder(),
                                code_->instructions[index_ + 1].jumpTarget,
                                NativeBranchKind::Zero});
      code_->bytecodeOffsetToNativeOffset[verification_.instructionOffsets[index_ + 1]] =
          code_->bytecodeOffsetToNativeOffset[bytecodeOffset_];
      ++index_;
    }
    return true;
#else
    return VisitRuntimeCall(helper);
#endif
  }

  bool VisitEqual(std::string& error) { return VisitComparison(error); }
  bool VisitGreater(std::string& error) { return VisitComparison(error); }
  bool VisitLess(std::string& error) { return VisitComparison(error); }

  bool VisitNot(std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!emitter_.emitBooleanNotOrRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineNot),
                                              error)) {
      return false;
    }
    RecordFallibleCall();
    return true;
#else
    return VisitRuntimeCall(reinterpret_cast<std::uintptr_t>(&minijsBaselineNot));
#endif
  }

  bool VisitJumpIfFalse(std::string& error) {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    if (!emitter_.emitBooleanTruthyOrRuntimeCall(
            reinterpret_cast<std::uintptr_t>(&minijsBaselinePeekTruthy), error)) {
      return false;
    }
#else
    emitRuntimeCall(emitter_, reinterpret_cast<std::uintptr_t>(&minijsBaselinePeekTruthy));
#endif
    branchPatches_.push_back(
        {emitter_.emitJumpIfZeroPlaceholder(), instruction_->jumpTarget, NativeBranchKind::Zero});
    epiloguePatches_.push_back({emitter_.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
    return true;
  }

  bool VisitJump() {
    branchPatches_.push_back({emitter_.emitJumpPlaceholder(), instruction_->jumpTarget,
                              NativeBranchKind::Unconditional});
    return true;
  }

  bool VisitLoop() {
    emitRuntimeCall(emitter_, reinterpret_cast<std::uintptr_t>(&minijsBaselineRecordLoopBackedge));
    RecordFallibleCall();
    branchPatches_.push_back({emitter_.emitJumpPlaceholder(), instruction_->jumpTarget,
                              NativeBranchKind::Unconditional});
    return true;
  }

  bool VisitReturn() {
    emitRuntimeCall(emitter_, reinterpret_cast<std::uintptr_t>(&minijsBaselineReturn));
    RecordFallibleCall();
    epiloguePatches_.push_back({emitter_.emitJumpPlaceholder(), false});
    return true;
  }

  const BytecodeFunction& function_;
  std::shared_ptr<BaselineCode>& code_;
  const BytecodeVerificationResult& verification_;
  Emitter& emitter_;
  std::vector<BranchPatch>& epiloguePatches_;
  std::vector<NativeBranchPatch>& branchPatches_;
  std::size_t& index_;
  std::size_t bytecodeOffset_;
  const DecodedInstruction* instruction_ = nullptr;
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  const std::vector<bool>& branchTargets_;
#endif
};

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
  code->bytecodeOffsetToNativeOffset.assign(function.chunk.code().size() + 1, kInvalidNativeOffset);
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
        branchTargets[verification.instructionOffsets[index + 1]])
      return 0;
    switch (second.opcode) {
      case Opcode::Add:
        return reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd);
      case Opcode::Sub:
        return reinterpret_cast<std::uintptr_t>(&minijsBaselineSub);
      case Opcode::Mul:
        return reinterpret_cast<std::uintptr_t>(&minijsBaselineMul);
      case Opcode::Div:
        // +0/-0 的除零错误仍执行原始路径；NaN 不按零处理。
        return function.chunk.constant(first.operands[0]).asNumber() == 0.0
                   ? 0
                   : reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv);
      case Opcode::Equal:
        return reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual);
      case Opcode::Greater:
        return reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater);
      case Opcode::Less:
        return reinterpret_cast<std::uintptr_t>(&minijsBaselineLess);
      default:
        return 0;
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
        if (!emitter.emitLocalTruthyOrRuntimeCall(instruction.operands[0], inlineError))
          return failure(inlineError);
        // 原假分支先弹出条件副本；现在无副本，跳到该 Pop 的下一条指令。
        branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                 verification.instructionOffsets[falseIndex + 1],
                                 NativeBranchKind::Zero});
        epiloguePatches.push_back(
            {emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
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
      if (constant.isString() || constant.isBoolean() || constant.isNull() ||
          constant.isUndefined()) {
        emitRuntimeCallWithU32(emitter,
                               reinterpret_cast<std::uintptr_t>(&minijsBaselineReturnConstant),
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
      if (!emitter.emitScalarConstantAssignment(
              instruction.operands[0], code->instructions[index + 1].operands[0],
              function.chunk.constant(instruction.operands[0]), inlineError))
        return failure(inlineError);
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
      if (!emitter.emitLocalCopyOrRuntimeCall(
              instruction.operands[0], code->instructions[index + 1].operands[0], inlineError))
        return failure(inlineError);
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
        independentEntry =
            independentEntry || branchTargets[verification.instructionOffsets[index + inner]];
      }
      if ((operation == Opcode::Negate || operation == Opcode::Not) &&
          code->instructions[index + 2].opcode == Opcode::SetLocal &&
          code->instructions[index + 3].opcode == Opcode::Pop && !independentEntry) {
        std::string inlineError;
        if (!emitter.emitLocalUnaryUpdateOrRuntimeCall(instruction.operands[0],
                                                       code->instructions[index + 2].operands[0],
                                                       operation, inlineError))
          return failure(inlineError);
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
      const bool isArithmetic =
          arithmetic.opcode == Opcode::Add || arithmetic.opcode == Opcode::Sub ||
          arithmetic.opcode == Opcode::Mul || arithmetic.opcode == Opcode::Div;
      const bool isComparison = arithmetic.opcode == Opcode::Equal ||
                                arithmetic.opcode == Opcode::Less ||
                                arithmetic.opcode == Opcode::Greater;
      bool independentEntry = false;
      for (std::size_t inner = 1; inner <= 4; ++inner) {
        independentEntry =
            independentEntry || branchTargets[verification.instructionOffsets[index + inner]];
      }
      if (rightOperand.opcode == Opcode::Constant && arithmetic.opcode == Opcode::Equal &&
          (function.chunk.constant(rightOperand.operands[0]).isBoolean() ||
           function.chunk.constant(rightOperand.operands[0]).isNull() ||
           function.chunk.constant(rightOperand.operands[0]).isUndefined()) &&
          assign.opcode == Opcode::SetLocal &&
          code->instructions[index + 4].opcode == Opcode::Pop && !independentEntry) {
        std::string inlineError;
        if (!emitter.emitScalarConstantComparisonAssignment(
                instruction.operands[0], rightOperand.operands[0],
                function.chunk.constant(rightOperand.operands[0]), assign.operands[0],
                inlineError)) {
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
      std::uintptr_t updateHelper =
          isArithmetic || isComparison ? constantBinaryHelper(index + 1) : 0;
      if ((isArithmetic || isComparison) && rightOperand.opcode == Opcode::GetLocal) {
        updateHelper = arithmetic.opcode == Opcode::Add
                           ? reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd)
                       : arithmetic.opcode == Opcode::Sub
                           ? reinterpret_cast<std::uintptr_t>(&minijsBaselineSub)
                       : arithmetic.opcode == Opcode::Mul
                           ? reinterpret_cast<std::uintptr_t>(&minijsBaselineMul)
                       : arithmetic.opcode == Opcode::Div
                           ? reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv)
                       : arithmetic.opcode == Opcode::Equal
                           ? reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual)
                       : arithmetic.opcode == Opcode::Less
                           ? reinterpret_cast<std::uintptr_t>(&minijsBaselineLess)
                           : reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater);
      }
      if (updateHelper != 0 && assign.opcode == Opcode::SetLocal &&
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
                                                constantRight, bits, assign.operands[0],
                                                arithmetic.opcode, updateHelper, inlineError)) {
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
          if (!emitter.emitNumberLocalUpdateOrRuntimeCall(instruction.operands[0], bits,
                                                          assign.operands[0], arithmetic.opcode,
                                                          updateHelper, inlineError)) {
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
        independentEntry =
            independentEntry || branchTargets[verification.instructionOffsets[index + inner]];
      }
      const auto falseIndex =
          code->bytecodeOffsetToInstructionIndex[code->instructions[index + 3].jumpTarget];
      if (!independentEntry && falseIndex < code->instructions.size() &&
          falseIndex + 1 < code->instructions.size() &&
          code->instructions[falseIndex].opcode == Opcode::Pop) {
        const auto constantIndex = code->instructions[index + 1].operands[0];
        const auto& constant = function.chunk.constant(constantIndex);
        std::string inlineError;
        if (constant.isNull() || constant.isUndefined()) {
          if (!emitter.emitEmptyConstantComparisonBranch(
                  instruction.operands[0], constantIndex,
                  constant.isNull() ? Value::nullTypeTag() : Value::undefinedTypeTag(),
                  inlineError)) {
            return failure(inlineError);
          }
        } else if (constant.isBoolean()) {
          // 编译期已确认 Boolean；isTruthy 这里只提取 0/1，不转换其它 Value。
          if (!emitter.emitBooleanConstantComparisonBranch(instruction.operands[0], constantIndex,
                                                           constant.isTruthy(), inlineError))
            return failure(inlineError);
        } else {
          const double number = constant.asNumber();
          std::uint64_t bits = 0;
          std::memcpy(&bits, &number, sizeof(bits));
          if (!emitter.emitComparisonBranch(instruction.operands[0], constantIndex,
                                            code->instructions[index + 2].opcode, true, bits,
                                            inlineError))
            return failure(inlineError);
        }
        branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                 verification.instructionOffsets[falseIndex + 1],
                                 NativeBranchKind::Zero});
        epiloguePatches.push_back(
            {emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
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
                                                             constant.isTruthy(), inlineError))
          return failure(inlineError);
      } else {
        if (!emitter.emitEmptyConstantComparisonExpression(
                instruction.operands[0], constantIndex,
                constant.isNull() ? Value::nullTypeTag() : Value::undefinedTypeTag(), inlineError))
          return failure(inlineError);
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
        case Opcode::Add:
          helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd);
          break;
        case Opcode::Sub:
          helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineSub);
          break;
        case Opcode::Mul:
          helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineMul);
          break;
        case Opcode::Div:
          helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv);
          break;
        case Opcode::Equal:
          helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineEqual);
          break;
        case Opcode::Greater:
          helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineGreater);
          break;
        case Opcode::Less:
          helper = reinterpret_cast<std::uintptr_t>(&minijsBaselineLess);
          break;
        default:
          break;
      }
      if (helper != 0) {
        std::string inlineError;
        const bool comparison =
            operation == Opcode::Equal || operation == Opcode::Greater || operation == Opcode::Less;
        if (comparison && index + 4 < code->instructions.size() &&
            code->instructions[index + 3].opcode == Opcode::JumpIfFalse &&
            code->instructions[index + 4].opcode == Opcode::Pop &&
            !branchTargets[verification.instructionOffsets[index + 3]] &&
            !branchTargets[verification.instructionOffsets[index + 4]]) {
          const auto falseIndex =
              code->bytecodeOffsetToInstructionIndex[code->instructions[index + 3].jumpTarget];
          if (falseIndex < code->instructions.size() &&
              falseIndex + 1 < code->instructions.size() &&
              code->instructions[falseIndex].opcode == Opcode::Pop) {
            if (!emitter.emitComparisonBranch(instruction.operands[0],
                                              code->instructions[index + 1].operands[0], operation,
                                              false, 0, inlineError))
              return failure(inlineError);
            branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                     verification.instructionOffsets[falseIndex + 1],
                                     NativeBranchKind::Zero});
            epiloguePatches.push_back(
                {emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
            for (std::size_t inner = 1; inner <= 4; ++inner) {
              code->bytecodeOffsetToNativeOffset[verification.instructionOffsets[index + inner]] =
                  code->bytecodeOffsetToNativeOffset[bytecodeOffset];
            }
            index += 4;
            continue;
          }
        }
        if (!emitter.emitLocalPairExpressionOrRuntimeCall(instruction.operands[0],
                                                          code->instructions[index + 1].operands[0],
                                                          operation, helper, inlineError)) {
          return failure(inlineError);
        }
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        const bool foldBranch = comparison && index + 3 < code->instructions.size() &&
                                code->instructions[index + 3].opcode == Opcode::JumpIfFalse &&
                                !branchTargets[verification.instructionOffsets[index + 3]];
        if (foldBranch) {
          emitter.emitLoadBooleanTopResult();
          branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                   code->instructions[index + 3].jumpTarget,
                                   NativeBranchKind::Zero});
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
                               operation == Opcode::Less) &&
                              index + 2 < code->instructions.size() &&
                              code->instructions[index + 2].opcode == Opcode::JumpIfFalse &&
                              !branchTargets[verification.instructionOffsets[index + 2]];
      const double number = function.chunk.constant(instruction.operands[0]).asNumber();
      std::uint64_t bits = 0;
      std::memcpy(&bits, &number, sizeof(bits));
      std::string inlineError;
      if (!emitter.emitNumberConstantBinaryOrRuntimeCall(bits, operation, binaryHelper, inlineError,
                                                         foldBranch)) {
        return failure(inlineError);
      }
      if (foldBranch) {
        branchPatches.push_back({emitter.emitJumpIfZeroPlaceholder(),
                                 code->instructions[index + 2].jumpTarget, NativeBranchKind::Zero});
        epiloguePatches.push_back(
            {emitter.emitJumpIfNotOnePlaceholder(), NativeBranchKind::NotOne});
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

    std::string visitError;
    BaselineBytecodeVisitor visitor(function, code, verification, emitter, epiloguePatches,
                                    branchPatches, index, bytecodeOffset
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
                                    ,
                                    branchTargets
#endif
    );
    if (!visitor.Visit(instruction, visitError)) {
      return failure(std::move(visitError));
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
