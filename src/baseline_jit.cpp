#include "minijs/baseline_jit.h"

#include <memory>
#include <string>
#include <utility>

#include "minijs/baseline_compiler.h"
#include "minijs/bytecode_decoder.h"

namespace minijs {
namespace {

BaselineCompileResult failure(std::string error) {
  BaselineCompileResult result;
  result.error = std::move(error);
  return result;
}

bool isBaselineSupported(Opcode opcode) {
  switch (opcode) {
    case Opcode::Constant:
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
    case Opcode::DefineGlobal:
    case Opcode::GetGlobal:
    case Opcode::SetGlobal:
    case Opcode::Array:
    case Opcode::GetIndex:
    case Opcode::SetIndex:
    case Opcode::Object:
    case Opcode::GetProperty:
    case Opcode::SetProperty:
    case Opcode::GetUpvalue:
    case Opcode::SetUpvalue:
    case Opcode::CloseUpvalue:
    case Opcode::GetCurrentClosure:
      return true;

    case Opcode::Call:
    case Opcode::MethodCall:
    case Opcode::Closure:
    case Opcode::Class:
    case Opcode::Method:
    case Opcode::StaticMethod:
    case Opcode::Inherit:
    case Opcode::SuperCall:
      return false;
  }

  return false;
}

BaselineCompileResult compileDecodedBaseline(const BytecodeFunction& function) {
  const BytecodeVerificationResult verification = verifyBytecode(function.chunk);
  if (!verification.valid) {
    return failure("bytecode verification failed: " + verification.error);
  }

  auto code = std::make_shared<BaselineCode>();
  code->bytecodeOffsetToInstructionIndex.assign(function.chunk.code().size() + 1,
                                                kInvalidInstructionIndex);
  code->instructions.reserve(verification.instructionOffsets.size());

  for (std::size_t index = 0; index < verification.instructionOffsets.size(); ++index) {
    const std::size_t offset = verification.instructionOffsets[index];
    code->bytecodeOffsetToInstructionIndex[offset] = index;
  }

  for (const std::size_t offset : verification.instructionOffsets) {
    DecodedInstruction instruction = decodeInstruction(function.chunk, offset);
    if (!isBaselineSupported(instruction.opcode)) {
      return failure(std::string("baseline does not support ") + opcodeName(instruction.opcode));
    }
    code->instructions.push_back(instruction);
  }

  BaselineCompileResult result;
  result.code = std::move(code);
  return result;
}

}  // namespace

BaselineCompileResult compileBaseline(const BytecodeFunction& function) {
  BaselineCompiler compiler;
  BaselineCompileResult nativeResult = compiler.compile(function);
  if (nativeResult.succeeded()) {
    return nativeResult;
  }

  return compileDecodedBaseline(function);
}

}  // namespace minijs
