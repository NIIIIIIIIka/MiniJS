#include "minijs/baseline_compiler.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "minijs/baseline_runtime.h"
#include "minijs/bytecode_decoder.h"
#include "minijs/value.h"

namespace minijs {
namespace {

struct BranchPatch {
  std::size_t offset = 0;
  bool conditional = false;
};

// 极小 ARM64 emitter：只负责写入当前 BaselineCompiler 需要的固定指令。
// 这里有意不抽象成通用 assembler，避免第一版 backend 扩散出过早接口。
class Arm64Emitter {
 public:
  std::size_t offset() const { return code_.size(); }
  const std::vector<std::uint8_t>& bytes() const { return code_; }

  // x0 初始保存 BaselineFrame*。helper 调用会使用 x0/x1，所以把 frame
  // 固定保存在 callee-saved x19，直到 epilogue 统一恢复。
  void emitPrologue() {
    emit32(0xA9BF7BFD);  // stp x29, x30, [sp, #-16]!
    emit32(0x910003FD);  // mov x29, sp
    emit32(0xA9BF53F3);  // stp x19, x20, [sp, #-16]!
    emit32(0xAA0003F3);  // mov x19, x0
  }

  // 所有 helper 失败路径和 Return 成功路径都会跳到同一个 epilogue，
  // 这样机器码出口只需要维护一份寄存器恢复逻辑。
  void emitEpilogue() {
    emit32(0xA8C153F3);  // ldp x19, x20, [sp], #16
    emit32(0xA8C17BFD);  // ldp x29, x30, [sp], #16
    emit32(0xD65F03C0);  // ret
  }

  //把 X19 寄存器的值复制到 X0 寄存器
  void emitMovFrameToX0() {
    emit32(0xAA1303E0);  // mov x0, x19
  }

  void emitMoveFrameToFirstArg() { emitMovFrameToX0(); }

  void emitMoveU32ToSecondArg(std::uint32_t value) { emitMovW1Imm32(value); }

  void emitLoadHelper(std::uintptr_t value) { emitLoadX16Imm64(value); }

  void emitCallHelper() { emitBlrX16(); }

  std::size_t emitJumpIfFalsePlaceholder() { return emitCbzW0Placeholder(); }

  std::size_t emitJumpPlaceholder() { return emitBPlaceholder(); }

  void emitMovW1Imm32(std::uint32_t value) {
    emitMovzW(1, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(1, static_cast<std::uint16_t>(value >> 16), 1);
    }
  }

  void emitLoadX16Imm64(std::uintptr_t value) {
    emitMovzX(16, static_cast<std::uint16_t>(value & 0xffff), 0);
    for (std::uint32_t shift = 1; shift < 4; ++shift) {
      const std::uint16_t part = static_cast<std::uint16_t>((value >> (shift * 16)) & 0xffff);
      if (part != 0) {
        emitMovkX(16, part, shift);
      }
    }
  }

  void emitBlrX16() {
    emit32(0xD63F0200);  // blr x16
  }

  std::size_t emitCbzW0Placeholder() {
    const std::size_t patchOffset = offset();
    emit32(0x34000000);  // cbz w0, <patch>
    return patchOffset;
  }

  std::size_t emitBPlaceholder() {
    const std::size_t patchOffset = offset();
    emit32(0x14000000);  // b <patch>
    return patchOffset;
  }

  // ARM64 分支立即数以当前指令地址为基准，单位是 4 字节指令。
  // emitter 先写 placeholder，等 epilogue offset 确定后统一回填。
  bool patchBranchTo(std::size_t branchOffset, std::size_t targetOffset, bool conditional,
                     std::string& error) {
    if (branchOffset + sizeof(std::uint32_t) > code_.size() ||
        branchOffset % sizeof(std::uint32_t) != 0 || targetOffset % sizeof(std::uint32_t) != 0) {
      error = "unaligned ARM64 branch patch";
      return false;
    }

    const std::int64_t displacement =
        static_cast<std::int64_t>(targetOffset) - static_cast<std::int64_t>(branchOffset);
    if (displacement % 4 != 0) {
      error = "unaligned ARM64 branch target";
      return false;
    }

    const std::int64_t immediate = displacement / 4;
    std::uint32_t instruction = 0;
    if (conditional) {
      if (immediate < -(1 << 18) || immediate >= (1 << 18)) {
        error = "ARM64 conditional branch target out of range";
        return false;
      }
      instruction = 0x34000000 | ((static_cast<std::uint32_t>(immediate) & 0x7ffff) << 5);
    } else {
      if (immediate < -(1 << 25) || immediate >= (1 << 25)) {
        error = "ARM64 branch target out of range";
        return false;
      }
      instruction = 0x14000000 | (static_cast<std::uint32_t>(immediate) & 0x03ffffff);
    }

    write32(branchOffset, instruction);
    return true;
  }

 private:
  void emit32(std::uint32_t instruction) {
    code_.push_back(static_cast<std::uint8_t>(instruction & 0xff));
    code_.push_back(static_cast<std::uint8_t>((instruction >> 8) & 0xff));
    code_.push_back(static_cast<std::uint8_t>((instruction >> 16) & 0xff));
    code_.push_back(static_cast<std::uint8_t>((instruction >> 24) & 0xff));
  }

  void write32(std::size_t patchOffset, std::uint32_t instruction) {
    code_[patchOffset] = static_cast<std::uint8_t>(instruction & 0xff);
    code_[patchOffset + 1] = static_cast<std::uint8_t>((instruction >> 8) & 0xff);
    code_[patchOffset + 2] = static_cast<std::uint8_t>((instruction >> 16) & 0xff);
    code_[patchOffset + 3] = static_cast<std::uint8_t>((instruction >> 24) & 0xff);
  }
    //Movz / Movk   是 movz 还是 movk 指令
    //W / X    宽度 W=32 位,X=64 位
    //发射一条 64 位的 movz 指令
  void emitMovzW(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0x52800000 | ((shift & 0x1) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  void emitMovkW(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0x72800000 | ((shift & 0x1) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  void emitMovzX(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0xD2800000 | ((shift & 0x3) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  void emitMovkX(std::uint32_t reg, std::uint16_t value, std::uint32_t shift) {
    emit32(0xF2800000 | ((shift & 0x3) << 21) |
           (static_cast<std::uint32_t>(value) << 5) | (reg & 0x1f));
  }

  std::vector<std::uint8_t> code_;
};

// 极小 Windows x64 emitter：同样只生成 helper-call stub。Windows x64 调用约定中，
// 第一个整数参数在 rcx，第二个在 rdx，调用者必须预留 32 字节 shadow space。
class WindowsX64Emitter {
 public:
  std::size_t offset() const { return code_.size(); }
  const std::vector<std::uint8_t>& bytes() const { return code_; }

  void emitPrologue() {
    emit8(0x53);                    // push rbx
    emit({0x48, 0x83, 0xEC, 0x20});  // sub rsp, 32
    emit({0x48, 0x89, 0xCB});        // mov rbx, rcx
  }

  void emitEpilogue() {
    emit({0x48, 0x83, 0xC4, 0x20});  // add rsp, 32
    emit8(0x5B);                    // pop rbx
    emit8(0xC3);                    // ret
  }

  void emitMoveFrameToFirstArg() {
    emit({0x48, 0x89, 0xD9});  // mov rcx, rbx
  }

  void emitMoveU32ToSecondArg(std::uint32_t value) {
    emit8(0xBA);  // mov edx, imm32
    emit32(value);
  }

  void emitLoadHelper(std::uintptr_t value) {
    emit({0x48, 0xB8});  // mov rax, imm64
    emit64(static_cast<std::uint64_t>(value));
  }

  void emitCallHelper() {
    emit({0xFF, 0xD0});  // call rax
  }

  std::size_t emitJumpIfFalsePlaceholder() {
    emit({0x85, 0xC0});  // test eax, eax
    const std::size_t patchOffset = offset();
    emit({0x0F, 0x84});  // jz rel32
    emit32(0);
    return patchOffset;
  }

  // 生成无条件前向跳转占位符，目标地址在 epilogue offset 确定后回填。
  std::size_t emitJumpPlaceholder() {
    const std::size_t patchOffset = offset();
    emit8(0xE9);  // jmp rel32
    emit32(0);
    return patchOffset;
  }

  // 如果栈顶是数字，就直接翻转 double 符号位；否则回退到 runtime helper。
  bool emitInlineNumberNegateOrRuntimeCall(std::uintptr_t functionAddress, std::string& error) {
    // double 的最高位是符号位，异或 signMask 即可在 +x 和 -x 之间切换。
    constexpr std::uint64_t signMask = 0x8000000000000000ULL;
    // imul r64, r64, imm32 要求 sizeof(Value) 能放进 32 位立即数。
    if (sizeof(Value) > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      error = "Value size is too large for x64 inline negate";
      return false;
    }

    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + BaselineFrame::stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0x85, 0xC0});  // test rax, rax
    const std::size_t emptyStackFallback = emitJzPlaceholder();
    emit({0x48, 0xFF, 0xC8});  // dec rax
    emit({0x48, 0x69, 0xC0});  // imul rax, rax, sizeof(Value)
    emit32(static_cast<std::uint32_t>(sizeof(Value)));
    emit({0x48, 0x8B, 0x8B});  // mov rcx, [rbx + BaselineFrame::stackData]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackData)));

    emit({0x48, 0x8D, 0x0C, 0x01});  // lea rcx, [rcx + rax]
    emit({0x81, 0xB9});              // cmp dword ptr [rcx + Value::typeOffset], Number
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    const std::size_t nonNumberFallback = emitJnePlaceholder();

    emit({0x48, 0xBA});  // mov rdx, signMask
    emit64(signMask);
    emit({0x48, 0x31, 0x91});  // xor qword ptr [rcx + Value::numberOffset], rdx
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xB8, 0x01, 0x00, 0x00, 0x00});  // mov eax, 1
    const std::size_t done = emitJumpPlaceholder();

    const std::size_t fallback = offset();
    patchBranchTo(emptyStackFallback, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    patchBranchTo(nonNumberFallback, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(functionAddress);
    emitCallHelper();

    const std::size_t doneOffset = offset();
    patchBranchTo(done, doneOffset, false, error);
    return error.empty();
  }

  // 如果栈顶两个值都是数字，就直接做 double 减法；否则回退到 runtime helper。
  bool emitInlineNumberSubOrRuntimeCall(std::uintptr_t functionAddress,
                                        std::uintptr_t syncStackSizeAddress,
                                        std::string& error) {
    // imul r64, r64, imm32 要求 sizeof(Value) 能放进 32 位立即数。
    if (sizeof(Value) > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      error = "Value size is too large for x64 inline sub";
      return false;
    }

    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + BaselineFrame::stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0x83, 0xF8, 0x02});  // cmp rax, 2
    const std::size_t notEnoughValuesFallback = emitJbPlaceholder();

    emit({0x48, 0x8B, 0x8B});  // mov rcx, [rbx + BaselineFrame::stackData]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackData)));

    emit({0x48, 0x89, 0xC2});  // mov rdx, rax
    emit({0x48, 0x83, 0xEA, 0x02});  // sub rdx, 2
    emit({0x48, 0x69, 0xD2});  // imul rdx, rdx, sizeof(Value)
    emit32(static_cast<std::uint32_t>(sizeof(Value)));
    emit({0x48, 0x8D, 0x14, 0x11});  // lea rdx, [rcx + rdx]

    emit({0x48, 0xFF, 0xC8});  // dec rax
    emit({0x48, 0x69, 0xC0});  // imul rax, rax, sizeof(Value)
    emit32(static_cast<std::uint32_t>(sizeof(Value)));
    emit({0x48, 0x8D, 0x0C, 0x01});  // lea rcx, [rcx + rax]

    emit({0x81, 0xBA});  // cmp dword ptr [rdx + Value::typeOffset], Number
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    const std::size_t leftNonNumberFallback = emitJnePlaceholder();

    emit({0x81, 0xB9});  // cmp dword ptr [rcx + Value::typeOffset], Number
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    const std::size_t rightNonNumberFallback = emitJnePlaceholder();

    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + Value::numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xF2, 0x0F, 0x5C, 0x81});  // subsd xmm0, [rcx + Value::numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xF2, 0x0F, 0x11, 0x82});  // movsd [rdx + Value::numberOffset], xmm0
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));

    emit({0x48, 0xFF, 0x8B});  // dec qword ptr [rbx + BaselineFrame::stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emitMoveFrameToFirstArg();
    emitLoadHelper(syncStackSizeAddress);
    emitCallHelper();
    const std::size_t done = emitJumpPlaceholder();

    const std::size_t fallback = offset();
    patchBranchTo(notEnoughValuesFallback, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    patchBranchTo(leftNonNumberFallback, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    patchBranchTo(rightNonNumberFallback, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(functionAddress);
    emitCallHelper();

    const std::size_t doneOffset = offset();
    patchBranchTo(done, doneOffset, false, error);
    return error.empty();
  }

  bool patchBranchTo(std::size_t branchOffset, std::size_t targetOffset, bool conditional,
                     std::string& error) {
    const std::size_t immediateOffset = branchOffset + (conditional ? 2 : 1);
    const std::size_t nextInstruction = immediateOffset + sizeof(std::int32_t);
    if (immediateOffset + sizeof(std::int32_t) > code_.size()) {
      error = "invalid x64 branch patch";
      return false;
    }

    const std::int64_t displacement =
        static_cast<std::int64_t>(targetOffset) - static_cast<std::int64_t>(nextInstruction);
    if (displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
      error = "x64 branch target out of range";
      return false;
    }

    write32(immediateOffset, static_cast<std::uint32_t>(
                                 static_cast<std::int32_t>(displacement)));
    return true;
  }

 private:
  void emit8(std::uint8_t byte) { code_.push_back(byte); }

  void emit(std::initializer_list<std::uint8_t> bytes) {
    code_.insert(code_.end(), bytes.begin(), bytes.end());
  }

  void emit32(std::uint32_t value) {
    code_.push_back(static_cast<std::uint8_t>(value & 0xff));
    code_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    code_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
    code_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
  }

  void emit64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
      code_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
  }

  // 生成 ZF=1 时跳转的占位符。
  std::size_t emitJzPlaceholder() {
    const std::size_t patchOffset = offset();
    emit({0x0F, 0x84});  // jz rel32
    emit32(0);
    return patchOffset;
  }

  // 生成 ZF=0 时跳转的占位符。
  std::size_t emitJnePlaceholder() {
    const std::size_t patchOffset = offset();
    emit({0x0F, 0x85});  // jne rel32
    emit32(0);
    return patchOffset;
  }

  // 生成 CF=1 时跳转的占位符，用于无符号比较后的 below 分支。
  std::size_t emitJbPlaceholder() {
    const std::size_t patchOffset = offset();
    emit({0x0F, 0x82});  // jb rel32
    emit32(0);
    return patchOffset;
  }

  void write32(std::size_t patchOffset, std::uint32_t value) {
    code_[patchOffset] = static_cast<std::uint8_t>(value & 0xff);
    code_[patchOffset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    code_[patchOffset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    code_[patchOffset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
  }

  std::vector<std::uint8_t> code_;
};

BaselineCompileResult failure(std::string error) {
  BaselineCompileResult result;
  result.error = std::move(error);
  return result;
}

// Runtime helper 统一返回 bool。调用者随后检查返回值并跳到 epilogue，
// 让 helper 内部捕获到的异常通过 frame->failed 传播回 C++ 边界。
template <typename Emitter>
void emitRuntimeCall(Emitter& emitter, std::uintptr_t functionAddress) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitLoadHelper(functionAddress);
  emitter.emitCallHelper();
}

}  // namespace

BaselineCompileResult BaselineCompiler::compile(const BytecodeFunction& function) const {
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
  }

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
  WindowsX64Emitter emitter;
#elif defined(__aarch64__) || defined(_M_ARM64)
  Arm64Emitter emitter;
#else
  return failure("baseline compiler has no native backend for this platform");
#endif
  std::vector<BranchPatch> epiloguePatches;
  emitter.emitPrologue();

  // bytecodeOffsetToNativeOffset 记录每条 bytecode 指令对应的机器码起点；
  // 以后做调试、safepoint 或 deopt 时会从这里回到 bytecode 语义位置。
  for (const std::size_t bytecodeOffset : verification.instructionOffsets) {
    DecodedInstruction instruction = decodeInstruction(function.chunk, bytecodeOffset);
    code->bytecodeOffsetToNativeOffset[bytecodeOffset] = emitter.offset();

    switch (instruction.opcode) {
      case Opcode::Constant:
        emitter.emitMoveFrameToFirstArg();
        emitter.emitMoveU32ToSecondArg(instruction.operands[0]);
        emitter.emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselinePushConstant));
        emitter.emitCallHelper();
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::GetLocal:
        emitter.emitMoveFrameToFirstArg();
        emitter.emitMoveU32ToSecondArg(instruction.operands[0]);
        emitter.emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
        emitter.emitCallHelper();
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Add:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineAdd));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Sub:
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
      {
        std::string inlineError;
        if (!emitter.emitInlineNumberSubOrRuntimeCall(
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSub),
                reinterpret_cast<std::uintptr_t>(&minijsBaselineSyncStackSize), inlineError)) {
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
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineMul));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Div:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineDiv));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

      case Opcode::Mod:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineMod));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        break;

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

      case Opcode::Return:
        emitRuntimeCall(emitter, reinterpret_cast<std::uintptr_t>(&minijsBaselineReturn));
        epiloguePatches.push_back({emitter.emitJumpIfFalsePlaceholder(), true});
        epiloguePatches.push_back({emitter.emitJumpPlaceholder(), false});
        break;

      default:
        return failure(std::string("baseline compiler does not support ") +
                       opcodeName(instruction.opcode));
    }

    code->instructions.push_back(instruction);
  }

  const std::size_t epilogueOffset = emitter.offset();
  code->bytecodeOffsetToNativeOffset[function.chunk.code().size()] = epilogueOffset;
  emitter.emitEpilogue();

  for (const BranchPatch& patch : epiloguePatches) {
    std::string patchError;
    if (!emitter.patchBranchTo(patch.offset, epilogueOffset, patch.conditional, patchError)) {
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
