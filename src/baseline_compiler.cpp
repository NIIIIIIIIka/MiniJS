#include "minijs/baseline_compiler.h"

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

enum class NativeBranchKind {
  Unconditional,
  Zero,
  NotOne,
};

struct BranchPatch {
  std::size_t offset = 0;
  NativeBranchKind kind = NativeBranchKind::Unconditional;

  BranchPatch() = default;
  BranchPatch(std::size_t offset, bool conditional)
      : offset(offset),
        kind(conditional ? NativeBranchKind::Zero : NativeBranchKind::Unconditional) {}
  BranchPatch(std::size_t offset, NativeBranchKind kind) : offset(offset), kind(kind) {}
};

struct NativeBranchPatch {
  std::size_t offset = 0;
  std::size_t bytecodeTarget = 0;
  NativeBranchKind kind = NativeBranchKind::Unconditional;
};

struct BinaryNumberGuardPatches {
  std::size_t notEnoughValues = 0;
  std::size_t leftNonNumber = 0;
  std::size_t rightNonNumber = 0;
  std::size_t rightZero = 0;
  bool hasRightZero = false;
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

  void emitMoveU32ToThirdArg(std::uint32_t value) { emitMovW2Imm32(value); }

  void emitMoveU32ToFourthArg(std::uint32_t value) { emitMovW3Imm32(value); }

  void emitLoadHelper(std::uintptr_t value) { emitLoadX16Imm64(value); }

  void emitCallHelper() { emitBlrX16(); }

  std::size_t emitJumpIfFalsePlaceholder() { return emitCbzW0Placeholder(); }

  std::size_t emitJumpPlaceholder() { return emitBPlaceholder(); }

  std::size_t emitJumpIfZeroPlaceholder() { return emitCbzW0Placeholder(); }

  std::size_t emitJumpIfNotOnePlaceholder() {
    emit32(0x7100041F);  // cmp w0, #1
    const std::size_t patchOffset = offset();
    emit32(0x54000001);  // b.ne <patch>
    return patchOffset;
  }

  void emitMovW1Imm32(std::uint32_t value) {
    emitMovzW(1, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(1, static_cast<std::uint16_t>(value >> 16), 1);
    }
  }

  void emitMovW2Imm32(std::uint32_t value) {
    emitMovzW(2, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(2, static_cast<std::uint16_t>(value >> 16), 1);
    }
  }

  void emitMovW3Imm32(std::uint32_t value) {
    emitMovzW(3, static_cast<std::uint16_t>(value & 0xffff), 0);
    if ((value >> 16) != 0) {
      emitMovkW(3, static_cast<std::uint16_t>(value >> 16), 1);
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
    return patchBranchTo(branchOffset,
                         targetOffset,
                         conditional ? NativeBranchKind::Zero
                                     : NativeBranchKind::Unconditional,
                         error);
  }

  bool patchBranchTo(std::size_t branchOffset, std::size_t targetOffset,
                     NativeBranchKind kind, std::string& error) {
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
    if (kind == NativeBranchKind::Zero) {
      if (immediate < -(1 << 18) || immediate >= (1 << 18)) {
        error = "ARM64 conditional branch target out of range";
        return false;
      }
      instruction = 0x34000000 | ((static_cast<std::uint32_t>(immediate) & 0x7ffff) << 5);
    } else if (kind == NativeBranchKind::NotOne) {
      if (immediate < -(1 << 18) || immediate >= (1 << 18)) {
        error = "ARM64 conditional branch target out of range";
        return false;
      }
      instruction = 0x54000001 | ((static_cast<std::uint32_t>(immediate) & 0x7ffff) << 5);
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

  void emitMoveU32ToThirdArg(std::uint32_t value) {
    emit({0x41, 0xB8});  // mov r8d, imm32
    emit32(value);
  }

  void emitMoveU32ToFourthArg(std::uint32_t value) {
    emit({0x41, 0xB9});  // mov r9d, imm32
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

  std::size_t emitJumpIfZeroPlaceholder() { return emitJumpIfFalsePlaceholder(); }

  std::size_t emitJumpIfNotOnePlaceholder() {
    emit({0x83, 0xF8, 0x01});  // cmp eax, 1
    const std::size_t patchOffset = offset();
    emit({0x0F, 0x85});  // jne rel32
    emit32(0);
    return patchOffset;
  }

  // slot 是相对当前调用帧的编号；rax 保存绝对槽编号，保留加法进位供 guard 检查。
  void emitLoadLocalAddress(std::uint32_t slot) {
    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + slotStart]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, slotStart)));
    emit({0x48, 0x05});  // add rax, slot
    emit32(slot);
  }

  void emitLocalPointerFromIndex() {
    emit({0x48, 0x69, 0xC0});  // imul rax, rax, sizeof(Value)
    emit32(static_cast<std::uint32_t>(sizeof(Value)));
    emit({0x48, 0x8B, 0x93});  // mov rdx, [rbx + stackData]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackData)));
    emit({0x48, 0x01, 0xC2});  // add rdx, rax
  }

  void emitLoadTopPointer() {
    emit({0x48, 0x8B, 0x8B});  // mov rcx, [rbx + stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0xFF, 0xC9});  // dec rcx
    emit({0x48, 0x69, 0xC9});  // imul rcx, rcx, sizeof(Value)
    emit32(static_cast<std::uint32_t>(sizeof(Value)));
    emit({0x48, 0x03, 0x8B});  // add rcx, [rbx + stackData]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackData)));
  }

  // Boolean 直接返回 0/1，其它类型交给三态 helper；不弹出条件值。
  bool emitBooleanTruthyOrRuntimeCall(std::uintptr_t helperAddress, std::string& error) {
    if (!validateInlineValueSize("boolean truthiness", error)) {
      return false;
    }
    // 等价 C++：if (frame.stackSize == 0) goto fallback;
    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0x85, 0xC0});  // test rax, rax
    const std::size_t emptyStack = emitJzPlaceholder();
    emitLoadTopPointer();
    // 等价 C++：if (top.type != Boolean) goto fallback;
    emit({0x81, 0xB9});  // cmp dword ptr [rcx + typeOffset], Boolean
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::booleanTypeTag());
    const std::size_t nonBoolean = emitJnePlaceholder();
    // 等价 C++：eax = top.boolean_ ? 1 : 0；不修改 Value 或栈深度。
    emit({0x0F, 0xB6, 0x81});  // movzx eax, byte ptr [rcx + booleanOffset]
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    const std::size_t done = emitJumpPlaceholder();

    const std::size_t fallback = offset();
    if (!patchBranchTo(emptyStack, fallback, true, error) ||
        !patchBranchTo(nonBoolean, fallback, true, error)) {
      return false;
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(helperAddress);
    emitCallHelper();  // 返回 0/1/2：falsey、truthy、失败。
    return patchBranchTo(done, offset(), false, error);
  }

  // Boolean 原地取反；其它类型使用完整 helper，保留原有 truthiness 语义。
  bool emitBooleanNotOrRuntimeCall(std::uintptr_t helperAddress, std::string& error) {
    if (!validateInlineValueSize("boolean not", error)) {
      return false;
    }
    // 等价 C++：if (frame.stackSize == 0) goto fallback;
    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0x85, 0xC0});  // test rax, rax
    const std::size_t emptyStack = emitJzPlaceholder();
    emitLoadTopPointer();
    // 等价 C++：if (top.type != Boolean) goto fallback；guard 前不修改栈。
    emit({0x81, 0xB9});  // cmp dword ptr [rcx + typeOffset], Boolean
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::booleanTypeTag());
    const std::size_t nonBoolean = emitJnePlaceholder();
    // 等价 C++：top.boolean_ = !top.boolean_；Boolean 的载荷是 0 或 1。
    emit({0x80, 0xB1});  // xor byte ptr [rcx + booleanOffset], 1
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    emit({0x01});
    emit({0xB8});  // mov eax, 1：执行成功，不是逻辑取反后的值。
    emit32(1);
    const std::size_t done = emitJumpPlaceholder();

    const std::size_t fallback = offset();
    if (!patchBranchTo(emptyStack, fallback, true, error) ||
        !patchBranchTo(nonBoolean, fallback, true, error)) {
      return false;
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(helperAddress);
    emitCallHelper();  // 完整 helper 返回成功状态；错误由统一 epilogue 处理。
    return patchBranchTo(done, offset(), false, error);
  }

  void emitMoveU64ToSecondArg(std::uint64_t value) {
    emit({0x48, 0xBA});  // mov rdx, imm64：第二个整数参数，保留全部 64 位。
    emit64(value);
  }

  // 编译时已确认常量是数字；运行时先构造栈槽，再写入 double 的原始位。
  bool emitNumberConstant(std::uint64_t numberBits, std::uintptr_t pushNumberSlotAddress,
                          std::string& error, int localSlot = -1) {
    if (!validateInlineValueSize("number constant", error)) {
      return false;
    }
    // helper 内部：stack_.emplace_back(0.0); refreshBaselineFrameStack(frame);
    emitMoveFrameToFirstArg();
    // 合并 GetLocal + Constant 时，helper 先压入局部值，再构造数字栈顶。
    if (localSlot >= 0) {
      emitMoveU32ToSecondArg(static_cast<std::uint32_t>(localSlot));
    }
    emitLoadHelper(pushNumberSlotAddress);
    emitCallHelper();
    const std::size_t failed = emitJumpIfFalsePlaceholder();

    // 等价 C++：rcx = &stack_[stack_.size() - 1]; 地址必须在 helper 后重新读取。
    emitLoadTopPointer();
    // 等价 C++：将常量的 64 位编码复制到栈顶 number_，不做整数到 double 转换。
    emit({0x48, 0xBA});  // mov rdx, numberBits
    emit64(numberBits);
    emit({0x48, 0x89, 0x91});  // mov [rcx + numberOffset], rdx
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xB8, 0x01, 0x00, 0x00, 0x00});  // mov eax, 1：本条 opcode 执行成功。
    // 构造失败时保持 false 返回值，跳过写入，由外层检查退出 native entry。
    return patchBranchTo(failed, offset(), true, error);
  }

  // 合并 Constant + 二元运算：常量只进入寄存器，不构造临时栈槽。
  bool emitNumberConstantBinaryOrRuntimeCall(std::uint64_t bits, Opcode operation,
                                             std::uintptr_t helperAddress,
                                             std::string& error, bool branchResult = false) {
    if (!validateInlineValueSize("constant binary", error)) return false;
    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0x85, 0xC0});  // test rax, rax：空栈走原始两条指令。
    const std::size_t empty = emitJzPlaceholder();
    emitLoadTopPointer();
    emit({0x81, 0xB9});  // cmp dword ptr [rcx + typeOffset], Number
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    const std::size_t nonNumber = emitJnePlaceholder();
    // 等价 C++：left = top.number_；right = 常量的 double 位编码。
    emit({0xF2, 0x0F, 0x10, 0x81});  // movsd xmm0, [rcx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0x48, 0xBA});  // mov rdx, bits
    emit64(bits);
    emit({0x66, 0x48, 0x0F, 0x6E, 0xCA});  // movq xmm1, rdx：复制位，不转换整数。
    if (operation == Opcode::Equal || operation == Opcode::Greater || operation == Opcode::Less) {
      emit({0x66, 0x0F, 0x2E, 0xC1});  // ucomisd xmm0, xmm1
      if (operation == Opcode::Greater) {
        emit({0x0F, 0x97, 0xC0});  // seta al：有序且大于。
      } else {
        // sete al 或 setb al：先取相等/小于条件，再用 PF 排除 unordered。
        emit({0x0F, operation == Opcode::Equal ? std::uint8_t{0x94} : std::uint8_t{0x92}, 0xC0});
        emit({0x0F, 0x9B, 0xC2});  // setnp dl：排除 NaN；不能写 cl，它属于栈顶指针。
        emit({0x20, 0xD0});        // and al, dl
      }
      emit({0x88, 0x81});  // mov byte ptr [rcx + booleanOffset], al
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0xC7, 0x81});  // mov dword ptr [rcx + typeOffset], Boolean
      emit32(static_cast<std::uint32_t>(Value::typeOffset()));
      emit32(Value::booleanTypeTag());
    } else {
      const std::uint8_t opcode = operation == Opcode::Add ? 0x58 : operation == Opcode::Sub ? 0x5C
                                 : operation == Opcode::Mul ? 0x59 : 0x5E;
      emit({0xF2, 0x0F, opcode, 0xC1});  // addsd/subsd/mulsd/divsd xmm0, xmm1
      emit({0xF2, 0x0F, 0x11, 0x81});  // movsd [rcx + numberOffset], xmm0
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    }
    if (branchResult) {
      emit({0x0F, 0xB6, 0xC0});  // movzx eax, al：比较的 0/1，清除高位。
    } else {
      emit({0xB8});  // mov eax, 1：成功；整个组合的净栈深度变化为 0。
      emit32(1);
    }
    const std::size_t done = emitJumpPlaceholder();
    const std::size_t fallback = offset();
    if (!patchBranchTo(empty, fallback, true, error) ||
        !patchBranchTo(nonNumber, fallback, true, error)) return false;
    // guard 失败前未改栈，先正常压入常量，再执行完整运算 helper。
    if (!emitNumberConstant(bits, reinterpret_cast<std::uintptr_t>(&minijsBaselinePushNumberSlot),
                            error)) return false;
    const std::size_t pushFailed = emitJumpIfFalsePlaceholder();
    emitMoveFrameToFirstArg();
    emitLoadHelper(helperAddress);
    emitCallHelper();
    if (branchResult) {
      const std::size_t operationFailed = emitJumpIfFalsePlaceholder();
      emitLoadBooleanTopResult(); // helper 已刷新栈；不能复用调用前的 rcx。
      const std::size_t fallbackDone = emitJumpPlaceholder();
      const std::size_t failed = offset();
      emit({0xB8});  // mov eax, 2：失败不是条件 false，交给统一 epilogue。
      emit32(2);
      return patchBranchTo(pushFailed, failed, true, error) &&
             patchBranchTo(operationFailed, failed, true, error) &&
             patchBranchTo(fallbackDone, offset(), false, error) &&
             patchBranchTo(done, offset(), false, error);
    }
    return patchBranchTo(pushFailed, offset(), true, error) &&
           patchBranchTo(done, offset(), false, error);
  }

  // Constant + SetLocal + Pop：Number/Boolean 同类型目标直接写载荷，无临时栈槽。
  bool emitScalarConstantAssignment(std::uint32_t constantIndex, std::uint32_t targetSlot,
                                    const Value& constant, std::string& error) {
    if (!validateInlineValueSize("constant local assignment", error)) return false;
    std::vector<std::size_t> fallbacks;
    emitLoadLocalAddress(targetSlot);
    fallbacks.push_back(emitJbPlaceholder());
    emit({0x48, 0x3B, 0x83});  // cmp rax, [rbx + stackSize]：检查目标存在。
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    fallbacks.push_back(emitJaePlaceholder());
    emitLocalPointerFromIndex();
    emit({0x81, 0xBA});  // cmp dword ptr [rdx + typeOffset], Number/Boolean
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(constant.isNumber() ? Value::numberTypeTag() : Value::booleanTypeTag());
    fallbacks.push_back(emitJnePlaceholder());
    if (constant.isNumber()) {
      const double number = constant.asNumber();
      std::uint64_t bits = 0;
      std::memcpy(&bits, &number, sizeof(bits));
      emit({0x48, 0xB8});  // mov rax, bits：数字的原始 64 位编码，不是整数转 double。
      emit64(bits);
      emit({0x48, 0x89, 0x82});  // mov [rdx + numberOffset], rax
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    } else {
      emit({0xC6, 0x82});  // mov byte ptr [rdx + booleanOffset], imm8：覆盖已有 Boolean。
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit8(constant.isTruthy() ? 1 : 0);
    }
    emit({0xB8});  // mov eax, 1：成功；栈深度未变。
    emit32(1);
    const auto done = emitJumpPlaceholder();
    const auto fallback = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(constantIndex);
    emitMoveU32ToThirdArg(targetSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineAssignConstant));
    emitCallHelper();
    return patchBranchTo(done, offset(), false, error);
  }

  // GetLocal + 数字 Constant + 算术 + SetLocal + Pop：直接写入存活的数字目标槽。
  bool emitNumberLocalUpdateOrRuntimeCall(std::uint32_t slot, std::uint64_t bits,
                                         std::uint32_t targetSlot, Opcode operation, std::uintptr_t helperAddress,
                                         std::string& error) {
    if (!validateInlineValueSize("local number update", error)) return false;
    std::vector<std::size_t> fallbacks;
    if (targetSlot != slot) {
      if (!emitLoadNumberLocalPair(targetSlot, targetSlot, fallbacks, error)) return false;
      emit({0x49, 0x89, 0xD2});  // mov r10, rdx：保留独立目标地址，后续 guard 不改 r10。
    }
    emitLoadLocalAddress(slot);
    fallbacks.push_back(emitJbPlaceholder()); // 槽编号加法溢出时交给完整读取 helper。
    emit({0x48, 0x3B, 0x83});  // cmp rax, [rbx + stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    fallbacks.push_back(emitJaePlaceholder());
    emitLocalPointerFromIndex();
    emit({0x81, 0xBA});  // cmp dword ptr [rdx + typeOffset], Number
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    fallbacks.push_back(emitJnePlaceholder());
    // guard 前不改栈；成功后无需临时 Value，也不需要同步真实栈深度。
    emit({0x48, 0x89, 0xD1});  // mov rcx, rdx：rcx 指向源局部槽。
    emit({0xF2, 0x0F, 0x10, 0x81});  // movsd xmm0, [rcx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0x48, 0xBA});  // mov rdx, bits
    emit64(bits);
    emit({0x66, 0x48, 0x0F, 0x6E, 0xCA});  // movq xmm1, rdx：装入常量的 double 位。
    const std::uint8_t opcode = operation == Opcode::Add ? 0x58 : operation == Opcode::Sub ? 0x5C
                               : operation == Opcode::Mul ? 0x59 : 0x5E;
    emit({0xF2, 0x0F, opcode, 0xC1});  // addsd/subsd/mulsd/divsd xmm0, xmm1
    // 源与目标同槽时写 rcx；独立目标写 r10，算完之前不覆盖任何源值。
    if (targetSlot == slot) emit({0xF2, 0x0F, 0x11, 0x81});
    else emit({0xF2, 0x41, 0x0F, 0x11, 0x82});  // movsd [r10 + numberOffset], xmm0
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xB8});  // mov eax, 1：五条指令的组合执行成功。
    emit32(1);
    const std::size_t done = emitJumpPlaceholder();
    const std::size_t fallback = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    // 未命中时按原顺序复制局部值、运算、赋值、弹出临时结果，不从头重试。
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(slot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
    emitCallHelper();
    std::vector<std::size_t> failures{emitJumpIfFalsePlaceholder()};
    if (!emitNumberConstantBinaryOrRuntimeCall(bits, operation, helperAddress, error)) return false;
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(targetSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal));
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselinePop));
    emitCallHelper();
    for (const auto patch : failures) {
      if (!patchBranchTo(patch, offset(), true, error)) return false;
    }
    return patchBranchTo(done, offset(), false, error);
  }

  // 两槽类型相同且存在时输出 rdx=左槽、rcx=右槽；guard 完成前不写入。
  bool emitLoadTypedLocalPair(std::uint32_t leftSlot, std::uint32_t rightSlot,
                               std::uint32_t typeTag,
                               std::vector<std::size_t>& fallbacks, std::string& error) {
    if (!validateInlineValueSize("typed local pair operands", error)) return false;
    for (int side = 0; side < 2; ++side) {
      const std::uint32_t slot = side == 0 ? leftSlot : rightSlot;
      emitLoadLocalAddress(slot);
      fallbacks.push_back(emitJbPlaceholder());
      emit({0x48, 0x3B, 0x83});  // cmp rax, [rbx + stackSize]
      emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
      fallbacks.push_back(emitJaePlaceholder());
      emitLocalPointerFromIndex();
      emit({0x81, 0xBA});  // cmp dword ptr [rdx + typeOffset], typeTag
      emit32(static_cast<std::uint32_t>(Value::typeOffset()));
      emit32(typeTag);
      fallbacks.push_back(emitJnePlaceholder());
      // 同槽时也保存左地址；直到两个值均读取完成才更新。
      if (side == 0) emit({0x49, 0x89, 0xD0}); // mov r8, rdx
    }
    emit({0x48, 0x89, 0xD1});  // mov rcx, rdx：右局部槽。
    emit({0x4C, 0x89, 0xC2});  // mov rdx, r8：恢复左局部槽。
    return true;
  }

  bool emitLoadNumberLocalPair(std::uint32_t leftSlot, std::uint32_t rightSlot,
                               std::vector<std::size_t>& fallbacks, std::string& error) {
    return emitLoadTypedLocalPair(leftSlot, rightSlot, Value::numberTypeTag(), fallbacks, error);
  }

  // 清理两条分支上的条件临时值时，直接读局部 Boolean；返回 0/1/2，不改栈。
  bool emitLocalTruthyOrRuntimeCall(std::uint32_t slot, std::string& error) {
    std::vector<std::size_t> fallbacks;
    if (!emitLoadTypedLocalPair(slot, slot, Value::booleanTypeTag(), fallbacks, error)) return false;
    emit({0x0F, 0xB6, 0x82});  // movzx eax, byte ptr [rdx + booleanOffset]：条件 0/1。
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    const auto done = emitJumpPlaceholder();
    const auto fallback = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(slot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineLocalTruthy));
    emitCallHelper();
    return patchBranchTo(done, offset(), false, error);
  }

  // GetLocal + Negate/Not + SetLocal + Pop：目标可与源不同，但必须同为 Number/Boolean。
  bool emitLocalUnaryUpdateOrRuntimeCall(std::uint32_t sourceSlot, std::uint32_t targetSlot,
                                        Opcode operation, std::string& error) {
    std::vector<std::size_t> fallbacks;
    const bool negate = operation == Opcode::Negate;
    if (!emitLoadTypedLocalPair(sourceSlot, targetSlot,
            negate ? Value::numberTypeTag() : Value::booleanTypeTag(), fallbacks, error)) return false;
    if (negate) {
      emit({0x48, 0x8B, 0x82});  // mov rax, [rdx + numberOffset]：读取源 double 位编码。
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
      emit({0x49, 0xB8});  // mov r8, signMask：double 最高位是符号位。
      emit64(0x8000000000000000ULL);
      emit({0x4C, 0x31, 0xC0});  // xor rax, r8：翻转符号，保留其它 63 位。
      emit({0x48, 0x89, 0x81});  // mov [rcx + numberOffset], rax：写入目标。
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    } else {
      emit({0x0F, 0xB6, 0x82});  // movzx eax, byte ptr [rdx + booleanOffset]
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0x34, 0x01});  // xor al, 1：Boolean 的 0/1 互换，不是通用 truthy 转换。
      emit({0x88, 0x81});  // mov byte ptr [rcx + booleanOffset], al
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    }
    emit({0xB8});  // mov eax, 1：栈深度未变，无同步 helper。
    emit32(1);
    const auto done = emitJumpPlaceholder();
    const auto fallback = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    // 完整重现读取、一元运算、赋值和丢弃；任一步失败后都不继续。
    std::vector<std::size_t> failures;
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(sourceSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitLoadHelper(negate ? reinterpret_cast<std::uintptr_t>(&minijsBaselineNegate)
                          : reinterpret_cast<std::uintptr_t>(&minijsBaselineNot));
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(targetSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal));
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselinePop));
    emitCallHelper();
    for (const auto patch : failures) {
      if (!patchBranchTo(patch, offset(), true, error)) return false;
    }
    return patchBranchTo(done, offset(), false, error);
  }

  // 等价于 target = source；同类型 Number/Boolean 直接复制载荷，不创建临时槽。
  bool emitLocalCopyOrRuntimeCall(std::uint32_t sourceSlot, std::uint32_t targetSlot,
                                 std::string& error) {
    std::vector<std::size_t> fallbacks;
    if (!emitLoadNumberLocalPair(sourceSlot, targetSlot, fallbacks, error)) return false;
    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + numberOffset]：读取源数字。
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xF2, 0x0F, 0x11, 0x81});  // movsd [rcx + numberOffset], xmm0：写入目标数字。
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xB8});  // mov eax, 1：栈深度不变，无需调用同步 helper。
    emit32(1);
    const std::size_t done = emitJumpPlaceholder();
    const std::size_t booleanPath = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, booleanPath, true, error)) return false;
    }
    // Number guard 失败前未改栈；重新检查槽边界和 Boolean 类型，不能复用未就绪的地址。
    fallbacks.clear();
    if (!emitLoadTypedLocalPair(sourceSlot, targetSlot, Value::booleanTypeTag(), fallbacks, error)) {
      return false;
    }
    emit({0x0F, 0xB6, 0x82});  // movzx eax, byte ptr [rdx + booleanOffset]：读源 0/1。
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    emit({0x88, 0x81});  // mov byte ptr [rcx + booleanOffset], al：写目标，不改类型。
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    emit({0xB8});  // mov eax, 1：赋值成功，不是刚复制的布尔真假值。
    emit32(1);
    const std::size_t booleanDone = emitJumpPlaceholder();
    const std::size_t fallback = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(sourceSlot);
    emitMoveU32ToThirdArg(targetSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineCopyLocal));
    emitCallHelper();
    return patchBranchTo(done, offset(), false, error) &&
           patchBranchTo(booleanDone, offset(), false, error);
  }

  // 右操作数也是局部值；所有槽通过 guard，运算完成后才写入目标。
  bool emitNumberLocalPairUpdateOrRuntimeCall(std::uint32_t leftSlot, std::uint32_t rightSlot,
                                             std::uint32_t targetSlot, Opcode operation, std::uintptr_t helperAddress,
                                             std::string& error) {
    std::vector<std::size_t> fallbacks;
    if (targetSlot != leftSlot) {
      if (!emitLoadNumberLocalPair(targetSlot, targetSlot, fallbacks, error)) return false;
      emit({0x49, 0x89, 0xD2});  // mov r10, rdx：目标可等于右槽，也可独立于两源槽。
    }
    if (!emitLoadNumberLocalPair(leftSlot, rightSlot, fallbacks, error)) return false;
    BinaryNumberGuardPatches zero;
    if (operation == Opcode::Div && !emitRightZeroGuard(zero, error)) return false;
    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    const std::uint8_t opcode = operation == Opcode::Add ? 0x58 : operation == Opcode::Sub ? 0x5C
                               : operation == Opcode::Mul ? 0x59 : 0x5E;
    emit({0xF2, 0x0F, opcode, 0x81});  // 算术 xmm0, [rcx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    if (targetSlot == leftSlot) emit({0xF2, 0x0F, 0x11, 0x82});
    else emit({0xF2, 0x41, 0x0F, 0x11, 0x82});  // movsd [r10 + numberOffset], xmm0
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xB8});  // mov eax, 1：无临时槽，无需同步栈深度。
    emit32(1);
    const std::size_t done = emitJumpPlaceholder();
    const std::size_t fallback = offset();
    if (zero.hasRightZero) fallbacks.push_back(zero.rightZero);
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    std::vector<std::size_t> failures;
    for (const auto slot : {leftSlot, rightSlot}) {
      emitMoveFrameToFirstArg();
      emitMoveU32ToSecondArg(slot);
      emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
      emitCallHelper();
      failures.push_back(emitJumpIfFalsePlaceholder());
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(helperAddress);
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(targetSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal));
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselinePop));
    emitCallHelper();
    for (const auto patch : failures) {
      if (!patchBranchTo(patch, offset(), true, error)) return false;
    }
    return patchBranchTo(done, offset(), false, error);
  }

  // 直接比较并返回三态分支值，不构造操作数或 Boolean 结果槽。
  bool emitComparisonBranch(std::uint32_t leftSlot, std::uint32_t rightOperand,
                            Opcode operation, bool constantRight, std::uint64_t bits, std::string& error) {
    std::vector<std::size_t> fallbacks;
    if (!emitLoadNumberLocalPair(leftSlot, constantRight ? leftSlot : rightOperand, fallbacks, error)) return false;
    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    if (constantRight) {
      emit({0x48, 0xB8});  // mov rax, bits：常量的原始 double 位编码。
      emit64(bits);
      emit({0x66, 0x48, 0x0F, 0x6E, 0xC8});  // movq xmm1, rax：复制位，不转换整数。
      emit({0x66, 0x0F, 0x2E, 0xC1});  // ucomisd xmm0, xmm1
    } else {
      emit({0x66, 0x0F, 0x2E, 0x81});  // ucomisd xmm0, [rcx + numberOffset]
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    }
    if (operation == Opcode::Greater) {
      emit({0x0F, 0x97, 0xC0});  // seta al：有序且大于。
    } else {
      emit({0x0F, operation == Opcode::Equal ? std::uint8_t{0x94} : std::uint8_t{0x92}, 0xC0});
      emit({0x0F, 0x9B, 0xC2});  // setnp dl：排除 NaN，源地址不再使用。
      emit({0x20, 0xD0});        // and al, dl
    }
    emit({0x0F, 0xB6, 0xC0});  // movzx eax, al：0/1 是比较结果，不是成功标志。
    const auto done = emitJumpPlaceholder();
    std::size_t booleanDone = 0;
    const bool booleanEquality = operation == Opcode::Equal && !constantRight;
    if (booleanEquality) {
      const auto booleanPath = offset();
      for (const auto patch : fallbacks) {
        if (!patchBranchTo(patch, booleanPath, true, error)) return false;
      }
      // 数字 guard 未命中前没有副作用；重新验证两槽均为 Boolean。
      fallbacks.clear();
      if (!emitLoadTypedLocalPair(leftSlot, rightOperand, Value::booleanTypeTag(), fallbacks, error)) {
        return false;
      }
      emit({0x0F, 0xB6, 0x82});  // movzx eax, byte ptr [rdx + booleanOffset]：读左 0/1。
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0x3A, 0x81});  // cmp al, byte ptr [rcx + booleanOffset]：只比较布尔载荷。
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0x0F, 0x94, 0xC0});  // sete al：相等为 1，不等为 0；无需 NaN 检查。
      emit({0x0F, 0xB6, 0xC0});  // movzx eax, al：false 是正常分支结果，不是失败。
      booleanDone = emitJumpPlaceholder();
    }
    const auto fallback = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(leftSlot);
    emitMoveU32ToThirdArg(rightOperand);
    emitMoveU32ToFourthArg(static_cast<std::uint32_t>(operation));
    emitLoadHelper(constantRight ? reinterpret_cast<std::uintptr_t>(&minijsBaselineCompareLocalConstantTruthy)
                                : reinterpret_cast<std::uintptr_t>(&minijsBaselineCompareLocalsTruthy));
    emitCallHelper();
    return patchBranchTo(done, offset(), false, error) &&
           (!booleanEquality || patchBranchTo(booleanDone, offset(), false, error));
  }

  // 比较赋值：仅更新已存在的 Boolean 目标；读取完成后再写，支持源目标别名。
  bool emitComparisonAssignment(std::uint32_t leftSlot, std::uint32_t rightSlot,
                                 bool constantRight, std::uint64_t bits, std::uint32_t targetSlot,
                                 Opcode operation, std::uintptr_t helperAddress, std::string& error) {
    std::vector<std::size_t> fallbacks;
    if (!emitLoadTypedLocalPair(targetSlot, targetSlot, Value::booleanTypeTag(), fallbacks, error)) return false;
    emit({0x49, 0x89, 0xD2});  // mov r10, rdx：保存 Boolean 目标地址，不跨 helper 使用。
    if (!emitLoadNumberLocalPair(leftSlot, constantRight ? leftSlot : rightSlot, fallbacks, error)) return false;
    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + numberOffset]：左数字。
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    if (constantRight) {
      emit({0x48, 0xB8});  // mov rax, bits：右常量的 double 原始位编码。
      emit64(bits);
      emit({0x66, 0x48, 0x0F, 0x6E, 0xC8});  // movq xmm1, rax：复制位，不转换整数。
      emit({0x66, 0x0F, 0x2E, 0xC1});  // ucomisd xmm0, xmm1
    } else {
      emit({0x66, 0x0F, 0x2E, 0x81});  // ucomisd xmm0, [rcx + numberOffset]
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    }
    if (operation == Opcode::Greater) {
      emit({0x0F, 0x97, 0xC0});  // seta al：有序且大于，NaN 时为 false。
    } else {
      emit({0x0F, operation == Opcode::Equal ? std::uint8_t{0x94} : std::uint8_t{0x92}, 0xC0});
      emit({0x0F, 0x9B, 0xC2});  // setnp dl：排除 unordered；源地址已不再使用。
      emit({0x20, 0xD0});        // and al, dl：相等/小于且不是 NaN。
    }
    emit({0x41, 0x88, 0x82});  // mov byte ptr [r10 + booleanOffset], al：仅写目标载荷。
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    emit({0xB8});  // mov eax, 1：执行成功；false 结果不是失败。
    emit32(1);
    const auto done = emitJumpPlaceholder();
    std::size_t booleanDone = 0;
    const bool booleanEquality = operation == Opcode::Equal && !constantRight;
    if (booleanEquality) {
      const auto booleanPath = offset();
      for (const auto patch : fallbacks) {
        if (!patchBranchTo(patch, booleanPath, true, error)) return false;
      }
      // 数字路径可能在目标 guard 就失败；重新验证目标并保存地址，不能沿用 r10。
      fallbacks.clear();
      if (!emitLoadTypedLocalPair(targetSlot, targetSlot, Value::booleanTypeTag(), fallbacks, error)) return false;
      emit({0x49, 0x89, 0xD2});  // mov r10, rdx：Boolean 目标，不跨 helper 持有。
      if (!emitLoadTypedLocalPair(leftSlot, rightSlot, Value::booleanTypeTag(), fallbacks, error)) return false;
      emit({0x0F, 0xB6, 0x82});  // movzx eax, byte ptr [rdx + booleanOffset]：读左 0/1。
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0x3A, 0x81});  // cmp al, byte ptr [rcx + booleanOffset]：比较右 0/1。
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0x0F, 0x94, 0xC0});  // sete al：相等为 1，不等为 0。
      emit({0x41, 0x88, 0x82});  // mov byte ptr [r10 + booleanOffset], al：两源读完才覆盖。
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0xB8});  // mov eax, 1：赋值成功；写入 false 也必须返回成功。
      emit32(1);
      booleanDone = emitJumpPlaceholder();
    }
    const auto fallback = offset();
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    std::vector<std::size_t> failures;
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(leftSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    if (constantRight) {
      // 复用已有常量二元路径，保持完整 Value 比较和数字常量位编码。
      if (!emitNumberConstantBinaryOrRuntimeCall(bits, operation, helperAddress, error)) return false;
    } else {
      emitMoveFrameToFirstArg();
      emitMoveU32ToSecondArg(rightSlot);
      emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
      emitCallHelper();
      failures.push_back(emitJumpIfFalsePlaceholder());
      emitMoveFrameToFirstArg();
      emitLoadHelper(helperAddress);
      emitCallHelper();
    }
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(targetSlot);
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineSetLocal));
    emitCallHelper();
    failures.push_back(emitJumpIfFalsePlaceholder());
    emitMoveFrameToFirstArg();
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselinePop));
    emitCallHelper();
    for (const auto patch : failures) {
      if (!patchBranchTo(patch, offset(), true, error)) return false;
    }
    return patchBranchTo(done, offset(), false, error) &&
           (!booleanEquality || patchBranchTo(booleanDone, offset(), false, error));
  }

  // GetLocal + GetLocal + 二元运算：只构造一个结果槽，不复制源局部值。
  bool emitNumberLocalPairExpressionOrRuntimeCall(std::uint32_t leftSlot, std::uint32_t rightSlot,
                                                 Opcode operation, std::uintptr_t helperAddress,
                                                 std::string& error) {
    std::vector<std::size_t> fallbacks;
    if (!emitLoadNumberLocalPair(leftSlot, rightSlot, fallbacks, error)) return false;
    BinaryNumberGuardPatches zero;
    if (operation == Opcode::Div && !emitRightZeroGuard(zero, error)) return false;
    emitMoveFrameToFirstArg();
    emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselinePushNumberSlot));
    emitCallHelper();
    const std::size_t pushFailed = emitJumpIfFalsePlaceholder();
    // helper 可能扩容：此前的 r8/rdx/rcx 均作废，重新读取两个局部槽地址。
    emitLoadLocalAddress(leftSlot);
    emitLocalPointerFromIndex();
    emit({0x49, 0x89, 0xD0});  // mov r8, rdx
    emitLoadLocalAddress(rightSlot);
    emitLocalPointerFromIndex();
    emit({0x48, 0x89, 0xD1});  // mov rcx, rdx：右槽
    emit({0x4C, 0x89, 0xC2});  // mov rdx, r8：左槽
    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    const bool comparison = operation == Opcode::Equal || operation == Opcode::Greater || operation == Opcode::Less;
    if (comparison) {
      emit({0x66, 0x0F, 0x2E, 0x81});  // ucomisd xmm0, [rcx + numberOffset]
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
      if (operation == Opcode::Greater) {
        emit({0x0F, 0x97, 0xC0});  // seta al
      } else {
        emit({0x0F, operation == Opcode::Equal ? std::uint8_t{0x94} : std::uint8_t{0x92}, 0xC0});
        emit({0x0F, 0x9B, 0xC2});  // setnp dl：排除 NaN，源地址已不再使用。
        emit({0x20, 0xD0});        // and al, dl
      }
      emitLoadTopPointer(); // 只改变 rcx，al 中的比较结果仍然有效。
      emit({0x88, 0x81});  // mov byte ptr [rcx + booleanOffset], al
      emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
      emit({0xC7, 0x81});  // mov dword ptr [rcx + typeOffset], Boolean
      emit32(static_cast<std::uint32_t>(Value::typeOffset()));
      emit32(Value::booleanTypeTag());
    } else {
      const std::uint8_t opcode = operation == Opcode::Add ? 0x58 : operation == Opcode::Sub ? 0x5C
                                 : operation == Opcode::Mul ? 0x59 : 0x5E;
      emit({0xF2, 0x0F, opcode, 0x81});  // 算术 xmm0, [rcx + numberOffset]
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
      emitLoadTopPointer();
      emit({0xF2, 0x0F, 0x11, 0x81});  // movsd [rcx + numberOffset], xmm0
      emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    }
    emit({0xB8});  // mov eax, 1：组合净压入一个结果，无需 SyncStackSize。
    emit32(1);
    const std::size_t done = emitJumpPlaceholder();
    const std::size_t fallback = offset();
    if (zero.hasRightZero) fallbacks.push_back(zero.rightZero);
    for (const auto patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) return false;
    }
    std::vector<std::size_t> failures;
    for (const auto slot : {leftSlot, rightSlot}) {
      emitMoveFrameToFirstArg();
      emitMoveU32ToSecondArg(slot);
      emitLoadHelper(reinterpret_cast<std::uintptr_t>(&minijsBaselineGetLocal));
      emitCallHelper();
      failures.push_back(emitJumpIfFalsePlaceholder());
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(helperAddress);
    emitCallHelper();
    for (const auto patch : failures) {
      if (!patchBranchTo(patch, offset(), true, error)) return false;
    }
    return patchBranchTo(pushFailed, offset(), true, error) &&
           patchBranchTo(done, offset(), false, error);
  }

  // 仅在比较成功后使用：比较一定产生 Boolean，无需重复类型和空栈 guard。
  void emitLoadBooleanTopResult() {
    emitLoadTopPointer();
    emit({0x0F, 0xB6, 0x81});  // movzx eax, byte ptr [rcx + booleanOffset]
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
  }

  bool emitNumberLocalOrRuntimeCall(std::uint32_t slot, bool read,
                                   std::uintptr_t helperAddress,
                                   std::uintptr_t pushNumberSlotAddress,
                                   std::string& error) {
    if (!validateInlineValueSize("local access", error)) {
      return false;
    }
    std::vector<std::size_t> fallbacks;
    emitLoadLocalAddress(slot);
    fallbacks.push_back(emitJbPlaceholder());  // add 的进位表示绝对槽编号溢出。
    emit({0x48, 0x3B, 0x83});  // cmp rax, [rbx + stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    fallbacks.push_back(emitJaePlaceholder());
    emitLocalPointerFromIndex();
    emit({0x81, 0xBA});  // cmp dword ptr [rdx + typeOffset], Number
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    fallbacks.push_back(emitJnePlaceholder());

    std::size_t pushFailed = 0;
    if (read) {
      // vector 扩容和 Value 构造仍由 C++ 管理，调用后必须重算地址。
      emitMoveFrameToFirstArg();
      emitLoadHelper(pushNumberSlotAddress);
      emitCallHelper();
      pushFailed = emitJumpIfFalsePlaceholder();
      emitLoadLocalAddress(slot);
      emitLocalPointerFromIndex();
    }
    // 有效的局部槽保证栈非空；写入还必须确认栈顶也是 Number。
    emitLoadTopPointer();
    if (!read) {
      emit({0x81, 0xB9});  // cmp dword ptr [rcx + typeOffset], Number
      emit32(static_cast<std::uint32_t>(Value::typeOffset()));
      emit32(Value::numberTypeTag());
      fallbacks.push_back(emitJnePlaceholder());
    }
    emit({0xF2, 0x0F, 0x10, static_cast<std::uint8_t>(read ? 0x82 : 0x81)});
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));  // movsd xmm0, [源 + numberOffset]
    emit({0xF2, 0x0F, 0x11, static_cast<std::uint8_t>(read ? 0x81 : 0x82)});
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));  // movsd [目标 + numberOffset], xmm0
    emit({0xB8, 0x01, 0x00, 0x00, 0x00});  // mov eax, 1
    const std::size_t done = emitJumpPlaceholder();

    const std::size_t fallback = offset();
    for (const std::size_t patch : fallbacks) {
      if (!patchBranchTo(patch, fallback, true, error)) {
        return false;
      }
    }
    emitMoveFrameToFirstArg();
    emitMoveU32ToSecondArg(slot);
    emitLoadHelper(helperAddress);
    emitCallHelper();
    const std::size_t end = offset();
    if (read && !patchBranchTo(pushFailed, end, true, error)) {
      return false;
    }
    return patchBranchTo(done, end, false, error);
  }

  // 如果栈顶是个数字，就把它变成负数；否则，回退到运行时。
  bool emitInlineNumberNegateOrRuntimeCall(std::uintptr_t functionAddress, std::string& error) {
    // 准备一个“符号位掩码”。double 的最高位就是符号位，异或 signMask = 取负。
    constexpr std::uint64_t signMask = 0x8000000000000000ULL;
    // 防御检查：下面有个 imul rax, rax, sizeof(Value) 用的是 32 位立即数，sizeof(Value) 必须塞得下。
    if (sizeof(Value) > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      error = "Value size is too large for x64 inline negate";
      return false;
    }

    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + BaselineFrame::stackSize]：rax = rbx(当前 BaselineFrame)->stackSize;
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0x85, 0xC0});  // test rax, rax
    const std::size_t emptyStackFallback = emitJzPlaceholder();  // test rax, rax + jz：if (rax == 0)（栈空），跳到回退。
    emit({0x48, 0xFF, 0xC8});  // dec rax：rax = rax - 1;
    emit({0x48, 0x69, 0xC0});  // imul rax, rax, sizeof(Value)
    emit32(static_cast<std::uint32_t>(sizeof(Value)));  // 计算栈顶元素的字节偏移：rax = rax * sizeof(Value);
    emit({0x48, 0x8B, 0x8B});  // mov rcx, [rbx + BaselineFrame::stackData]：Value* rcx = rbx->stackData;
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackData)));

    emit({0x48, 0x8D, 0x0C, 0x01});  // lea rcx, [rcx + rax]：rcx = rcx + rax，即栈顶指针
    emit({0x81, 0xB9});              // cmp dword ptr [rcx + Value::typeOffset], Number：if (rcx->type != Number) goto 回退;
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    const std::size_t nonNumberFallback = emitJnePlaceholder();

    // 栈顶 = -栈顶。
    emit({0x48, 0xBA});  // mov rdx, signMask：rcx->number ^= rdx;
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

  bool validateInlineValueSize(std::string_view context, std::string& error) {
    // imul r64, r64, imm32 要求 sizeof(Value) 能放进 32 位立即数。
    if (sizeof(Value) > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
      error = std::string("Value size is too large for x64 ") + std::string(context);
      return false;
    }
    return true;
  }

  // 生成二元 number guard。成功后 rdx 指向 left，rcx 指向 right。
  bool emitLoadBinaryNumberOperands(BinaryNumberGuardPatches& patches,
                                    std::string_view context,
                                    std::string& error) {
    if (!validateInlineValueSize(context, error)) {
      return false;
    }
    emit({0x48, 0x8B, 0x83});  // mov rax, [rbx + BaselineFrame::stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emit({0x48, 0x83, 0xF8, 0x02});  // cmp rax, 2
    patches.notEnoughValues = emitJbPlaceholder();

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
    patches.leftNonNumber = emitJnePlaceholder();

    emit({0x81, 0xB9});  // cmp dword ptr [rcx + Value::typeOffset], Number
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::numberTypeTag());
    patches.rightNonNumber = emitJnePlaceholder();
    return true;
  }

  bool emitRightZeroGuard(BinaryNumberGuardPatches& patches, std::string& error) {
    // 除法和取模需要保留除零错误：右操作数是 0 时回退到通用 helper。
    // NaN 走 unordered 分支，不会被当作 0。
    emit({0xF2, 0x0F, 0x10, 0x89});  // movsd xmm1, [rcx + Value::numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0x66, 0x0F, 0x57, 0xD2});  // xorpd xmm2, xmm2
    emit({0x66, 0x0F, 0x2E, 0xCA});  // ucomisd xmm1, xmm2
    const std::size_t unorderedContinue = emitJpPlaceholder();
    patches.rightZero = emitJzPlaceholder();
    patches.hasRightZero = true;
    patchBranchTo(unorderedContinue, offset(), true, error);
    return error.empty();
  }

  bool patchBinaryNumberFallbacks(const BinaryNumberGuardPatches& patches,
                                  std::size_t fallback,
                                  std::string& error) {
    patchBranchTo(patches.notEnoughValues, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    patchBranchTo(patches.leftNonNumber, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    patchBranchTo(patches.rightNonNumber, fallback, true, error);
    if (!error.empty()) {
      return false;
    }
    if (patches.hasRightZero) {
      patchBranchTo(patches.rightZero, fallback, true, error);
      if (!error.empty()) {
        return false;
      }
    }
    return true;
  }

  // 如果栈顶两个值都是数字，就直接做二元 double 运算；否则回退到 runtime helper。
  bool emitInlineNumberBinaryOrRuntimeCall(std::uintptr_t functionAddress,
                                           std::uintptr_t syncStackSizeAddress,
                                           std::uint8_t sseOpcode,
                                           std::string_view operationName,
                                           bool fallbackOnRightZero,
                                           std::string& error) {
    BinaryNumberGuardPatches patches;
    if (!emitLoadBinaryNumberOperands(patches,
                                      std::string("inline ") + std::string(operationName),
                                      error)) {
      return false;
    }
    if (fallbackOnRightZero && !emitRightZeroGuard(patches, error)) {
      return false;
    }

    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + Value::numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0xF2, 0x0F, sseOpcode, 0x81});  // <op>sd xmm0, [rcx + Value::numberOffset]
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
    if (!patchBinaryNumberFallbacks(patches, fallback, error)) {
      return false;
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(functionAddress);
    emitCallHelper();

    const std::size_t doneOffset = offset();
    patchBranchTo(done, doneOffset, false, error);
    return error.empty();
  }

  // 数字比较将左操作数槽改为 Boolean，再同步二元运算的弹栈效果。
  bool emitInlineNumberComparisonOrRuntimeCall(std::uintptr_t functionAddress,
                                               std::uintptr_t syncStackSizeAddress,
                                               Opcode comparison,
                                               std::string& error) {
    BinaryNumberGuardPatches patches;
    if (!emitLoadBinaryNumberOperands(patches, "inline comparison", error)) {
      return false;
    }

    // rdx 指向左操作数，rcx 指向右操作数；guard 完成前不修改栈。
    emit({0xF2, 0x0F, 0x10, 0x82});  // movsd xmm0, [rdx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    emit({0x66, 0x0F, 0x2E, 0x81});  // ucomisd xmm0, [rcx + numberOffset]
    emit32(static_cast<std::uint32_t>(Value::numberOffset()));
    if (comparison == Opcode::Greater) {
      emit({0x0F, 0x97, 0xC0});  // seta al：CF=0 且 ZF=0，NaN 时自然为 false。
    } else {
      if (comparison == Opcode::Equal) {
        emit({0x0F, 0x94, 0xC0});  // sete al：ZF=1，包括 unordered。
      } else {
        emit({0x0F, 0x92, 0xC0});  // setb al：CF=1，包括 unordered。
      }
      emit({0x0F, 0x9B, 0xC1});  // setnp cl：PF=0，排除任一操作数为 NaN。
      emit({0x20, 0xC8});        // and al, cl：排除 NaN，相等或小于条件才有效。
    }
    emit({0x88, 0x82});  // mov byte ptr [rdx + booleanOffset], al
    emit32(static_cast<std::uint32_t>(Value::booleanOffset()));
    emit({0xC7, 0x82});  // mov dword ptr [rdx + typeOffset], Boolean
    emit32(static_cast<std::uint32_t>(Value::typeOffset()));
    emit32(Value::booleanTypeTag());

    emit({0x48, 0xFF, 0x8B});  // dec qword ptr [rbx + stackSize]
    emit32(static_cast<std::uint32_t>(offsetof(BaselineFrame, stackSize)));
    emitMoveFrameToFirstArg();
    emitLoadHelper(syncStackSizeAddress);
    emitCallHelper();
    const std::size_t done = emitJumpPlaceholder();

    const std::size_t fallback = offset();
    if (!patchBinaryNumberFallbacks(patches, fallback, error)) {
      return false;
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(functionAddress);
    emitCallHelper();
    patchBranchTo(done, offset(), false, error);
    return error.empty();
  }

  // 如果栈顶两个值都是数字，并且需要时确认右操作数非 0，就调用窄 helper；
  // 否则回退到完整 runtime helper，保留类型错误和除零错误语义。
  bool emitGuardedNumberBinaryRuntimeCall(std::uintptr_t guardedFunctionAddress,
                                          std::uintptr_t fallbackFunctionAddress,
                                          std::string_view operationName,
                                          bool fallbackOnRightZero,
                                          std::string& error) {
    BinaryNumberGuardPatches patches;
    if (!emitLoadBinaryNumberOperands(patches,
                                      std::string("guarded ") + std::string(operationName),
                                      error)) {
      return false;
    }
    if (fallbackOnRightZero && !emitRightZeroGuard(patches, error)) {
      return false;
    }

    emitMoveFrameToFirstArg();
    emitLoadHelper(guardedFunctionAddress);
    emitCallHelper();
    const std::size_t done = emitJumpPlaceholder();

    const std::size_t fallback = offset();
    if (!patchBinaryNumberFallbacks(patches, fallback, error)) {
      return false;
    }
    emitMoveFrameToFirstArg();
    emitLoadHelper(fallbackFunctionAddress);
    emitCallHelper();

    const std::size_t doneOffset = offset();
    patchBranchTo(done, doneOffset, false, error);
    return error.empty();
  }

  bool patchBranchTo(std::size_t branchOffset, std::size_t targetOffset, bool conditional,
                     std::string& error) {
    return patchBranchTo(branchOffset,
                         targetOffset,
                         conditional ? NativeBranchKind::Zero
                                     : NativeBranchKind::Unconditional,
                         error);
  }

  bool patchBranchTo(std::size_t branchOffset, std::size_t targetOffset,
                     NativeBranchKind kind, std::string& error) {
    const bool conditional = kind != NativeBranchKind::Unconditional;
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

  // 生成 PF=1 时跳转的占位符，用于跳过 NaN 的除零 fallback。
  std::size_t emitJpPlaceholder() {
    const std::size_t patchOffset = offset();
    emit({0x0F, 0x8A});  // jp rel32
    emit32(0);
    return patchOffset;
  }

  // 生成 CF=1 时跳转的占位符，用于无符号比较后的 below 分支。
  std::size_t emitJaePlaceholder() {
    const std::size_t patchOffset = offset();
    emit({0x0F, 0x83});  // jae rel32
    emit32(0);
    return patchOffset;
  }

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

template <typename Emitter>
void emitRuntimeCallWithU32(Emitter& emitter, std::uintptr_t functionAddress,
                            std::uint32_t argument) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitMoveU32ToSecondArg(argument);
  emitter.emitLoadHelper(functionAddress);
  emitter.emitCallHelper();
}

template <typename Emitter>
void emitRuntimeCallWithTwoU32(Emitter& emitter, std::uintptr_t functionAddress,
                               std::uint32_t firstArgument,
                               std::uint32_t secondArgument) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitMoveU32ToSecondArg(firstArgument);
  emitter.emitMoveU32ToThirdArg(secondArgument);
  emitter.emitLoadHelper(functionAddress);
  emitter.emitCallHelper();
}

template <typename Emitter>
void emitRuntimeCallWithThreeU32(Emitter& emitter, std::uintptr_t functionAddress,
                                 std::uint32_t firstArgument,
                                 std::uint32_t secondArgument,
                                 std::uint32_t thirdArgument) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitMoveU32ToSecondArg(firstArgument);
  emitter.emitMoveU32ToThirdArg(secondArgument);
  emitter.emitMoveU32ToFourthArg(thirdArgument);
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
        function.chunk.constant(code->instructions[index + 1].operands[0]).isNumber() &&
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
        const double number = function.chunk.constant(constantIndex).asNumber();
        std::uint64_t bits = 0;
        std::memcpy(&bits, &number, sizeof(bits));
        std::string inlineError;
        if (!emitter.emitComparisonBranch(instruction.operands[0], constantIndex,
                code->instructions[index + 2].opcode, true, bits, inlineError)) return failure(inlineError);
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
        if (!emitter.emitNumberLocalPairExpressionOrRuntimeCall(
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
