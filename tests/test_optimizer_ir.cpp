#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include "minijs/bytecode_function.h"
#include "minijs/chunk.h"
#include "minijs/compiler.h"
#include "minijs/optimizer_ir.h"
#include "minijs/optimizer_deopt.h"
#include "minijs/optimizer_safepoint.h"
#include "minijs/optimized_code.h"
#include "minijs/parser.h"
#include "minijs/runtime_error.h"
#include "minijs/vm.h"
#include "test_framework.h"

namespace {

struct CompiledProgramAndFunction {
  minijs::Chunk program;
  std::shared_ptr<minijs::BytecodeFunction> function;
};

CompiledProgramAndFunction compileFunctionProgram(std::string_view source,
                                                  const std::string& functionName) {
  minijs::Parser parser(source);
  minijs::Program program = parser.parseProgram();
  EXPECT(parser.diagnostics().empty());
  minijs::Compiler compiler;
  CompiledProgramAndFunction result;
  result.program = compiler.compileProgram(program);
  for (const minijs::Value& constant : result.program.constants()) {
    if (constant.isBytecodeFunction() &&
        constant.asBytecodeFunction()->name == functionName) {
      result.function = constant.asBytecodeFunction();
      break;
    }
  }
  EXPECT(result.function != nullptr);
  return result;
}

void writeForwardJump(minijs::Chunk& chunk, minijs::Opcode opcode, std::uint16_t distance) {
  chunk.writeOpcode(opcode);
  chunk.writeByte(static_cast<std::uint8_t>((distance >> 8) & 0xff));
  chunk.writeByte(static_cast<std::uint8_t>(distance & 0xff));
}

void testStraightLineCfg() {
  minijs::Chunk chunk;
  const auto constant = chunk.addConstant(minijs::Value(1.0));
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(static_cast<std::uint8_t>(constant));
  chunk.writeOpcode(minijs::Opcode::Return);

  const auto result = minijs::buildOptimizerControlFlowGraph(chunk);
  EXPECT(result.valid);
  EXPECT(result.graph.blocks.size() == 1);
  EXPECT(result.graph.blocks[0].instructions.size() == 2);
  EXPECT(result.graph.blocks[0].successors.empty());
}

void testDiamondCfg() {
  minijs::Chunk chunk;
  const auto condition = chunk.addConstant(minijs::Value(true));
  const auto thenValue = chunk.addConstant(minijs::Value(1.0));
  const auto elseValue = chunk.addConstant(minijs::Value(2.0));

  chunk.writeOpcode(minijs::Opcode::Constant);  // 0
  chunk.writeByte(static_cast<std::uint8_t>(condition));
  writeForwardJump(chunk, minijs::Opcode::JumpIfFalse, 6);  // 2 -> 11
  chunk.writeOpcode(minijs::Opcode::Pop);                    // 5
  chunk.writeOpcode(minijs::Opcode::Constant);               // 6
  chunk.writeByte(static_cast<std::uint8_t>(thenValue));
  writeForwardJump(chunk, minijs::Opcode::Jump, 3);  // 8 -> 14
  chunk.writeOpcode(minijs::Opcode::Pop);             // 11
  chunk.writeOpcode(minijs::Opcode::Constant);        // 12
  chunk.writeByte(static_cast<std::uint8_t>(elseValue));
  chunk.writeOpcode(minijs::Opcode::Return);  // 14

  const auto result = minijs::buildOptimizerControlFlowGraph(chunk);
  EXPECT(result.valid);
  EXPECT(result.graph.blocks.size() == 4);
  EXPECT(result.graph.blocks[0].successors.size() == 2);
  EXPECT(result.graph.blocks[3].predecessors.size() == 2);
}

void testLoopCfgHasBackedge() {
  minijs::Chunk chunk;
  const auto condition = chunk.addConstant(minijs::Value(true));
  chunk.writeOpcode(minijs::Opcode::Constant);  // 0
  chunk.writeByte(static_cast<std::uint8_t>(condition));
  writeForwardJump(chunk, minijs::Opcode::JumpIfFalse, 4);  // 2 -> 9
  chunk.writeOpcode(minijs::Opcode::Pop);                    // 5
  chunk.writeOpcode(minijs::Opcode::Loop);                   // 6 -> 0
  chunk.writeByte(0);
  chunk.writeByte(9);
  chunk.writeOpcode(minijs::Opcode::Return);  // 9

  const auto result = minijs::buildOptimizerControlFlowGraph(chunk);
  EXPECT(result.valid);
  EXPECT(result.graph.blocks.size() == 3);
  const auto& loopBody = result.graph.blocks[1];
  EXPECT(std::find(loopBody.successors.begin(), loopBody.successors.end(), 0) !=
         loopBody.successors.end());

  const auto stack = minijs::analyzeOptimizerStackDepths(result.graph, 0);
  EXPECT(stack.valid);
  EXPECT(stack.entryDepths[0] == 0);
  EXPECT(stack.entryDepths[1] == 1);
  EXPECT(stack.entryDepths[2] == 1);
}

void testInvalidBytecodeIsRejected() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(42);
  const auto result = minijs::buildOptimizerControlFlowGraph(chunk);
  EXPECT(!result.valid);
  EXPECT(!result.error.empty());
}

void testStackUnderflowIsRejected() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::Pop);
  chunk.writeOpcode(minijs::Opcode::Return);
  const auto cfg = minijs::buildOptimizerControlFlowGraph(chunk);
  EXPECT(cfg.valid);
  const auto stack = minijs::analyzeOptimizerStackDepths(cfg.graph, 0);
  EXPECT(!stack.valid);
  EXPECT(stack.error.find("underflow") != std::string::npos);
}

void testMergeWithDifferentStackDepthIsRejected() {
  minijs::Chunk chunk;
  const auto condition = chunk.addConstant(minijs::Value(true));
  const auto value = chunk.addConstant(minijs::Value(1.0));
  chunk.writeOpcode(minijs::Opcode::Constant);  // 0
  chunk.writeByte(static_cast<std::uint8_t>(condition));
  writeForwardJump(chunk, minijs::Opcode::JumpIfFalse, 5);  // 2 -> 10
  chunk.writeOpcode(minijs::Opcode::Constant);               // 5
  chunk.writeByte(static_cast<std::uint8_t>(value));
  writeForwardJump(chunk, minijs::Opcode::Jump, 0);  // 7 -> 10
  chunk.writeOpcode(minijs::Opcode::Return);         // 10

  const auto cfg = minijs::buildOptimizerControlFlowGraph(chunk);
  EXPECT(cfg.valid);
  const auto stack = minijs::analyzeOptimizerStackDepths(cfg.graph, 0);
  EXPECT(!stack.valid);
  EXPECT(stack.error.find("mismatch") != std::string::npos);
}

void testUnsupportedOpcodeIsRejectedByOptimizerAnalysis() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::GetGlobal);
  chunk.writeByte(
      static_cast<std::uint8_t>(chunk.addConstant(minijs::Value(std::string("x")))));
  chunk.writeOpcode(minijs::Opcode::Return);
  const auto cfg = minijs::buildOptimizerControlFlowGraph(chunk);
  EXPECT(cfg.valid);
  const auto stack = minijs::analyzeOptimizerStackDepths(cfg.graph, 0);
  EXPECT(!stack.valid);
  EXPECT(stack.error.find("does not support") != std::string::npos);
}

void testStraightLineBytecodeBecomesValueIr() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::GetLocal);
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::GetLocal);
  chunk.writeByte(1);
  chunk.writeOpcode(minijs::Opcode::Add);
  chunk.writeOpcode(minijs::Opcode::Return);

  const auto ir = minijs::buildOptimizerIr(chunk, 2);
  EXPECT(ir.valid);
  EXPECT(ir.function.blocks.size() == 1);
  EXPECT(ir.function.valueCount == 3);
  const auto& instructions = ir.function.blocks[0].instructions;
  EXPECT(instructions.size() == 4);
  EXPECT(instructions[0].opcode == minijs::OptimizerIrOpcode::Parameter);
  EXPECT(instructions[1].opcode == minijs::OptimizerIrOpcode::Parameter);
  EXPECT(instructions[2].opcode == minijs::OptimizerIrOpcode::Add);
  EXPECT(instructions[2].inputs.size() == 2);
  EXPECT(instructions[3].opcode == minijs::OptimizerIrOpcode::Return);
  EXPECT(instructions[3].inputs[0] == instructions[2].result);
}

void testLoopLocalCreatesLoopCarriedPhi() {
  minijs::Chunk chunk;
  const auto limit = chunk.addConstant(minijs::Value(10.0));
  const auto one = chunk.addConstant(minijs::Value(1.0));
  chunk.writeOpcode(minijs::Opcode::GetLocal);   // 0
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Constant);  // 2
  chunk.writeByte(static_cast<std::uint8_t>(limit));
  chunk.writeOpcode(minijs::Opcode::Less);                    // 4
  writeForwardJump(chunk, minijs::Opcode::JumpIfFalse, 12);  // 5 -> 20
  chunk.writeOpcode(minijs::Opcode::Pop);                     // 8
  chunk.writeOpcode(minijs::Opcode::GetLocal);                // 9
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Constant);  // 11
  chunk.writeByte(static_cast<std::uint8_t>(one));
  chunk.writeOpcode(minijs::Opcode::Add);       // 13
  chunk.writeOpcode(minijs::Opcode::SetLocal);  // 14
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Pop);   // 16
  chunk.writeOpcode(minijs::Opcode::Loop);  // 17 -> 0
  chunk.writeByte(0);
  chunk.writeByte(20);
  chunk.writeOpcode(minijs::Opcode::Pop);       // 20
  chunk.writeOpcode(minijs::Opcode::GetLocal);  // 21
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Return);  // 23

  const auto ir = minijs::buildOptimizerIr(chunk, 1);
  EXPECT(ir.valid);
  EXPECT(ir.function.blocks.size() == 3);
  const auto& header = ir.function.blocks[0];
  const auto phi = std::find_if(header.instructions.begin(), header.instructions.end(),
                                [](const minijs::OptimizerIrInstruction& instruction) {
                                  return instruction.opcode == minijs::OptimizerIrOpcode::Phi;
                                });
  EXPECT(phi != header.instructions.end());
  EXPECT(phi->inputs.size() == 2);
  EXPECT(header.entryValues[0] == phi->result);
  EXPECT(phi->inputs[0] != phi->inputs[1]);
}

void testIrInterpreterMatchesVmForArithmeticFunction() {
  auto compiled = compileFunctionProgram(
      "function twiceSum(a, b) { return (a + b) * 2; } twiceSum(3, 4);",
      "twiceSum");
  minijs::VM vm;
  const minijs::Value vmResult = vm.run(compiled.program);
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk,
                                            compiled.function->params.size());
  EXPECT(ir.valid);
  const auto interpreted = minijs::executeOptimizerIr(
      ir.function, compiled.function->chunk, {minijs::Value(3.0), minijs::Value(4.0)});
  EXPECT(interpreted.completed);
  EXPECT(interpreted.value.equals(vmResult));
  EXPECT(interpreted.value.equals(minijs::Value(14.0)));
}

void testIrInterpreterMatchesVmForLoopFunction() {
  auto compiled = compileFunctionProgram(
      "function sumTo(n) { let i = 0; let sum = 0; "
      "while (i < n) { sum = sum + i; i = i + 1; } return sum; } sumTo(10);",
      "sumTo");
  minijs::VM vm;
  const minijs::Value vmResult = vm.run(compiled.program);
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk,
                                            compiled.function->params.size());
  EXPECT(ir.valid);
  const auto interpreted = minijs::executeOptimizerIr(
      ir.function, compiled.function->chunk, {minijs::Value(10.0)});
  if (!interpreted.completed) {
    std::cerr << "loop IR execution failed: " << interpreted.error << '\n';
  }
  EXPECT(interpreted.completed);
  EXPECT(interpreted.value.equals(vmResult));
  EXPECT(interpreted.value.equals(minijs::Value(45.0)));
}

void testIrInterpreterRejectsWrongArgumentCount() {
  minijs::Chunk chunk;
  chunk.writeOpcode(minijs::Opcode::GetLocal);
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Return);
  const auto ir = minijs::buildOptimizerIr(chunk, 1);
  EXPECT(ir.valid);
  const auto interpreted = minijs::executeOptimizerIr(ir.function, chunk, {});
  EXPECT(!interpreted.completed);
  EXPECT(interpreted.error.find("argument count") != std::string::npos);
}

void testTypeInferenceFindsNumericArithmetic() {
  auto compiled = compileFunctionProgram(
      "function twiceSum(a, b) { return (a + b) * 2; } twiceSum(3, 4);",
      "twiceSum");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 2);
  EXPECT(ir.valid);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, compiled.function->chunk,
      {minijs::OptimizerType::Number, minijs::OptimizerType::Number});
  EXPECT(types.valid);
  EXPECT(std::all_of(types.types.begin(), types.types.end(), [](minijs::OptimizerType type) {
    return type == minijs::OptimizerType::Number;
  }));
}

void testTypeInferenceStabilizesLoopPhi() {
  auto compiled = compileFunctionProgram(
      "function sumTo(n) { let i = 0; let sum = 0; "
      "while (i < n) { sum = sum + i; i = i + 1; } return sum; } sumTo(10);",
      "sumTo");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 1);
  EXPECT(ir.valid);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, compiled.function->chunk, {minijs::OptimizerType::Number});
  EXPECT(types.valid);
  bool sawNumberPhi = false;
  for (const auto& block : ir.function.blocks) {
    for (const auto& instruction : block.instructions) {
      if (instruction.opcode == minijs::OptimizerIrOpcode::Phi &&
          types.types[instruction.result] == minijs::OptimizerType::Number) {
        sawNumberPhi = true;
      }
    }
  }
  EXPECT(sawNumberPhi);
}

void testTypeInferenceJoinsDifferentPhiTypesToUnknown() {
  auto compiled = compileFunctionProgram(
      "function choose(flag) { let value = 1; if (flag) { value = true; } return value; } "
      "choose(false);",
      "choose");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 1);
  EXPECT(ir.valid);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, compiled.function->chunk, {minijs::OptimizerType::Boolean});
  EXPECT(types.valid);
  bool sawUnknownPhi = false;
  for (const auto& block : ir.function.blocks) {
    for (const auto& instruction : block.instructions) {
      if (instruction.opcode == minijs::OptimizerIrOpcode::Phi &&
          types.types[instruction.result] == minijs::OptimizerType::Unknown) {
        sawUnknownPhi = true;
      }
    }
  }
  EXPECT(sawUnknownPhi);
}

void testTypeInferenceDoesNotCallStringAddNumeric() {
  minijs::Chunk chunk;
  const auto text = chunk.addConstant(minijs::Value(std::string("x")));
  const auto number = chunk.addConstant(minijs::Value(1.0));
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(static_cast<std::uint8_t>(text));
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(static_cast<std::uint8_t>(number));
  chunk.writeOpcode(minijs::Opcode::Add);
  chunk.writeOpcode(minijs::Opcode::Return);
  const auto ir = minijs::buildOptimizerIr(chunk, 0);
  EXPECT(ir.valid);
  const auto types = minijs::inferOptimizerTypes(ir.function, chunk, {});
  EXPECT(types.valid);
  EXPECT(types.types.back() == minijs::OptimizerType::String);
}

void testTypeInferencePreservesUnknownParameter() {
  minijs::Chunk chunk;
  const auto one = chunk.addConstant(minijs::Value(1.0));
  chunk.writeOpcode(minijs::Opcode::GetLocal);
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(static_cast<std::uint8_t>(one));
  chunk.writeOpcode(minijs::Opcode::Add);
  chunk.writeOpcode(minijs::Opcode::Return);
  const auto ir = minijs::buildOptimizerIr(chunk, 1);
  EXPECT(ir.valid);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, chunk, {minijs::OptimizerType::Unknown});
  EXPECT(types.valid);
  EXPECT(types.types.back() == minijs::OptimizerType::Unknown);
}

void testNumericArithmeticIsOptimizerEligible() {
  auto compiled = compileFunctionProgram(
      "function twiceSum(a, b) { return (a + b) * 2; } twiceSum(3, 4);",
      "twiceSum");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 2);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, compiled.function->chunk,
      {minijs::OptimizerType::Number, minijs::OptimizerType::Number});
  const auto eligibility = minijs::analyzeOptimizerEligibility(
      ir.function, compiled.function->chunk, types);
  EXPECT(eligibility.eligible);
  EXPECT(eligibility.failure == minijs::OptimizerEligibilityFailure::None);
  EXPECT(eligibility.error.empty());
}

void testNumericLoopIsOptimizerEligible() {
  auto compiled = compileFunctionProgram(
      "function sumTo(n) { let i = 0; let sum = 0; "
      "while (i < n) { sum = sum + i; i = i + 1; } return sum; } sumTo(10);",
      "sumTo");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 1);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, compiled.function->chunk, {minijs::OptimizerType::Number});
  const auto eligibility = minijs::analyzeOptimizerEligibility(
      ir.function, compiled.function->chunk, types);
  EXPECT(eligibility.eligible);
}

void testUnknownParameterIsNotOptimizerEligible() {
  minijs::Chunk chunk;
  const auto one = chunk.addConstant(minijs::Value(1.0));
  chunk.writeOpcode(minijs::Opcode::GetLocal);
  chunk.writeByte(0);
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(static_cast<std::uint8_t>(one));
  chunk.writeOpcode(minijs::Opcode::Add);
  chunk.writeOpcode(minijs::Opcode::Return);
  const auto ir = minijs::buildOptimizerIr(chunk, 1);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, chunk, {minijs::OptimizerType::Unknown});
  const auto eligibility = minijs::analyzeOptimizerEligibility(ir.function, chunk, types);
  EXPECT(!eligibility.eligible);
  EXPECT(eligibility.failure == minijs::OptimizerEligibilityFailure::NonNumberParameter);
  EXPECT(eligibility.error.find("parameter 0") != std::string::npos);
}

void testStringAddIsNotOptimizerEligible() {
  minijs::Chunk chunk;
  const auto text = chunk.addConstant(minijs::Value(std::string("x")));
  const auto one = chunk.addConstant(minijs::Value(1.0));
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(static_cast<std::uint8_t>(text));
  chunk.writeOpcode(minijs::Opcode::Constant);
  chunk.writeByte(static_cast<std::uint8_t>(one));
  chunk.writeOpcode(minijs::Opcode::Add);
  chunk.writeOpcode(minijs::Opcode::Return);
  const auto ir = minijs::buildOptimizerIr(chunk, 0);
  const auto types = minijs::inferOptimizerTypes(ir.function, chunk, {});
  const auto eligibility = minijs::analyzeOptimizerEligibility(ir.function, chunk, types);
  EXPECT(!eligibility.eligible);
  EXPECT(eligibility.failure == minijs::OptimizerEligibilityFailure::UnsupportedConstant);
  EXPECT(!eligibility.error.empty());
}

void testMixedPhiIsNotOptimizerEligible() {
  auto compiled = compileFunctionProgram(
      "function choose() { let flag = true; let value = 1; "
      "if (flag) { value = true; } return value; } choose();",
      "choose");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 0);
  const auto types = minijs::inferOptimizerTypes(ir.function, compiled.function->chunk, {});
  const auto eligibility = minijs::analyzeOptimizerEligibility(
      ir.function, compiled.function->chunk, types);
  EXPECT(!eligibility.eligible);
  EXPECT(eligibility.failure == minijs::OptimizerEligibilityFailure::UnsupportedPhiType);
  EXPECT(eligibility.value != minijs::InvalidOptimizerValue);
}

void testVmProfilesStableNumberParameters() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1000);
  minijs::Parser parser(
      "function identity(value) { return value; } identity(1); identity(2);");
  minijs::Program program = parser.parseProgram();
  EXPECT(parser.diagnostics().empty());
  minijs::Compiler compiler;
  (void)vm.run(compiler.compileProgram(program));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("identity");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->optimize.parameterTypes.size() == 1);
  EXPECT(feedback->optimize.parameterTypes[0] == minijs::ObservedType::Number);
}

void testVmProfilesPolymorphicParameters() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1000);
  minijs::Parser parser(
      "function identity(value) { return value; } identity(1); identity(true);");
  minijs::Program program = parser.parseProgram();
  EXPECT(parser.diagnostics().empty());
  minijs::Compiler compiler;
  (void)vm.run(compiler.compileProgram(program));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("identity");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->optimize.parameterTypes.size() == 1);
  EXPECT(feedback->optimize.parameterTypes[0] == minijs::ObservedType::Polymorphic);
}

void testNumberGuardsAcceptProfiledShapeAndRejectWrongType() {
  auto compiled = compileFunctionProgram(
      "function twiceSum(a, b) { return (a + b) * 2; } twiceSum(3, 4);",
      "twiceSum");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 2);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, compiled.function->chunk,
      {minijs::OptimizerType::Number, minijs::OptimizerType::Number});
  const auto eligibility = minijs::analyzeOptimizerEligibility(
      ir.function, compiled.function->chunk, types);
  const auto guarded = minijs::insertOptimizerNumberGuards(ir.function, eligibility);
  EXPECT(guarded.valid);
  EXPECT(guarded.guardCount == 2);

  const auto accepted = minijs::executeOptimizerIr(
      guarded.function, compiled.function->chunk,
      {minijs::Value(3.0), minijs::Value(4.0)});
  EXPECT(accepted.completed);
  EXPECT(!accepted.guardFailed);
  EXPECT(accepted.value.equals(minijs::Value(14.0)));

  const auto rejected = minijs::executeOptimizerIr(
      guarded.function, compiled.function->chunk,
      {minijs::Value(std::string("3")), minijs::Value(4.0)});
  EXPECT(!rejected.completed);
  EXPECT(rejected.guardFailed);
  EXPECT(rejected.error.find("parameter 0") != std::string::npos);
}

void testLoopEntryNumberGuardPrecedesComputation() {
  auto compiled = compileFunctionProgram(
      "function sumTo(n) { let i = 0; let sum = 0; "
      "while (i < n) { sum = sum + i; i = i + 1; } return sum; } sumTo(10);",
      "sumTo");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 1);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, compiled.function->chunk, {minijs::OptimizerType::Number});
  const auto eligibility = minijs::analyzeOptimizerEligibility(
      ir.function, compiled.function->chunk, types);
  const auto guarded = minijs::insertOptimizerNumberGuards(ir.function, eligibility);
  EXPECT(guarded.valid);
  EXPECT(guarded.guardCount == 1);
  const auto accepted = minijs::executeOptimizerIr(
      guarded.function, compiled.function->chunk, {minijs::Value(10.0)});
  EXPECT(accepted.completed);
  EXPECT(accepted.value.equals(minijs::Value(45.0)));
}

void testOptimizerPassesFoldNumericConstants() {
  auto compiled = compileFunctionProgram(
      "function folded() { return (2 + 3) * 4; } folded();", "folded");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 0);
  const auto optimized = minijs::optimizeOptimizerIr(ir.function, compiled.function->chunk);
  EXPECT(optimized.valid);
  EXPECT(optimized.stats.constantsFolded >= 2);
  EXPECT(optimized.stats.deadInstructionsRemoved >= 2);
  const auto executed = minijs::executeOptimizerIr(
      optimized.function, compiled.function->chunk, {});
  EXPECT(executed.completed);
  EXPECT(executed.value.equals(minijs::Value(20.0)));
}

void testOptimizerPassesFoldConstantBranch() {
  auto compiled = compileFunctionProgram(
      "function choose() { if (true) { return 1; } else { return 2; } } choose();",
      "choose");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 0);
  const auto optimized = minijs::optimizeOptimizerIr(ir.function, compiled.function->chunk);
  EXPECT(optimized.valid);
  EXPECT(optimized.stats.branchesFolded >= 1);
  const auto executed = minijs::executeOptimizerIr(
      optimized.function, compiled.function->chunk, {});
  EXPECT(executed.completed);
  EXPECT(executed.value.equals(minijs::Value(1.0)));
}

void testOptimizerPassesRemoveRedundantPhi() {
  auto compiled = compileFunctionProgram(
      "function pick(flag, value) { if (flag) { value = value; } return value; } "
      "pick(true, 9);",
      "pick");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 2);
  const auto optimized = minijs::optimizeOptimizerIr(ir.function, compiled.function->chunk);
  EXPECT(optimized.valid);
  EXPECT(optimized.stats.phisRemoved >= 1);
  const auto executed = minijs::executeOptimizerIr(
      optimized.function, compiled.function->chunk,
      {minijs::Value(true), minijs::Value(9.0)});
  EXPECT(executed.completed);
  EXPECT(executed.value.equals(minijs::Value(9.0)));
}

void testOptimizerPassesRemoveDeadPureComputation() {
  auto compiled = compileFunctionProgram(
      "function dead(value) { let unused = 1 + 2; return value; } dead(7);",
      "dead");
  const auto ir = minijs::buildOptimizerIr(compiled.function->chunk, 1);
  const auto optimized = minijs::optimizeOptimizerIr(ir.function, compiled.function->chunk);
  EXPECT(optimized.valid);
  EXPECT(optimized.stats.deadInstructionsRemoved >= 1);
  const auto executed = minijs::executeOptimizerIr(
      optimized.function, compiled.function->chunk, {minijs::Value(7.0)});
  EXPECT(executed.completed);
  EXPECT(executed.value.equals(minijs::Value(7.0)));
}

minijs::OptimizerLoweringResult lowerNumericFunction(
    const std::shared_ptr<minijs::BytecodeFunction>& function,
    std::size_t parameterCount) {
  const auto ir = minijs::buildOptimizerIr(function->chunk, parameterCount);
  EXPECT(ir.valid);
  std::vector<minijs::OptimizerType> parameterTypes(
      parameterCount, minijs::OptimizerType::Number);
  const auto types = minijs::inferOptimizerTypes(
      ir.function, function->chunk, parameterTypes);
  EXPECT(types.valid);
  const auto eligibility = minijs::analyzeOptimizerEligibility(
      ir.function, function->chunk, types);
  EXPECT(eligibility.eligible);
  const auto guarded = minijs::insertOptimizerNumberGuards(
      ir.function, eligibility);
  EXPECT(guarded.valid);
  const auto optimized = minijs::optimizeOptimizerIr(
      guarded.function, function->chunk);
  EXPECT(optimized.valid);
  auto lowered = minijs::lowerOptimizerNumberIr(
      optimized.function, function->chunk, types);
  if (!lowered.valid) std::cerr << "number lowering failed: " << lowered.error << '\n';
  return lowered;
}

void testNumberLoweringUsesUnboxedArithmeticAndAbiBoxes() {
  auto compiled = compileFunctionProgram(
      "function twiceSum(a, b) { return (a + b) * 2; } twiceSum(3, 4);",
      "twiceSum");
  const auto lowered = lowerNumericFunction(compiled.function, 2);
  EXPECT(lowered.valid);
  bool sawAdd = false;
  bool sawMul = false;
  bool sawBox = false;
  for (const auto& block : lowered.function.blocks) {
    for (const auto& instruction : block.instructions) {
      sawAdd = sawAdd || instruction.opcode == minijs::OptimizerLoweredOpcode::AddNumber;
      sawMul = sawMul || instruction.opcode == minijs::OptimizerLoweredOpcode::MulNumber;
      sawBox = sawBox || instruction.opcode == minijs::OptimizerLoweredOpcode::BoxNumber;
    }
  }
  EXPECT(sawAdd);
  EXPECT(sawMul);
  EXPECT(sawBox);
  const auto executed = minijs::executeOptimizerLoweredIr(
      lowered.function, {minijs::Value(3.0), minijs::Value(4.0)});
  EXPECT(executed.completed);
  EXPECT(executed.value.equals(minijs::Value(14.0)));
}

void testNumberLoweringPreservesLoopPhis() {
  auto compiled = compileFunctionProgram(
      "function sumTo(n) { let i = 0; let sum = 0; "
      "while (i < n) { sum = sum + i; i = i + 1; } return sum; } sumTo(10);",
      "sumTo");
  const auto lowered = lowerNumericFunction(compiled.function, 1);
  EXPECT(lowered.valid);
  const auto executed = minijs::executeOptimizerLoweredIr(
      lowered.function, {minijs::Value(10.0)});
  EXPECT(executed.completed);
  EXPECT(executed.value.equals(minijs::Value(45.0)));
}

void testNumberLoweringRetainsEntryGuard() {
  auto compiled = compileFunctionProgram(
      "function identity(value) { return value; } identity(1);", "identity");
  const auto lowered = lowerNumericFunction(compiled.function, 1);
  EXPECT(lowered.valid);
  const auto rejected = minijs::executeOptimizerLoweredIr(
      lowered.function, {minijs::Value(true)});
  EXPECT(!rejected.completed);
  EXPECT(rejected.guardFailed);
  EXPECT(rejected.error.find("parameter 0") != std::string::npos);
}

void testLoweredVerifierRejectsRepresentationMismatch() {
  auto compiled = compileFunctionProgram(
      "function less(a, b) { return a < b; } less(1, 2);", "less");
  auto lowered = lowerNumericFunction(compiled.function, 2);
  EXPECT(lowered.valid);
  const auto executed = minijs::executeOptimizerLoweredIr(
      lowered.function, {minijs::Value(1.0), minijs::Value(2.0)});
  EXPECT(executed.completed);
  EXPECT(executed.value.equals(minijs::Value(true)));
  bool corrupted = false;
  for (auto& block : lowered.function.blocks) {
    for (auto& instruction : block.instructions) {
      if (instruction.opcode == minijs::OptimizerLoweredOpcode::LessNumber) {
        lowered.function.representations[instruction.inputs[0]] =
            minijs::OptimizerRepresentation::Boolean;
        corrupted = true;
        break;
      }
    }
    if (corrupted) break;
  }
  EXPECT(corrupted);
  const auto verified = minijs::verifyOptimizerLoweredIr(lowered.function);
  EXPECT(!verified.valid);
  EXPECT(verified.error.find("numeric comparison") != std::string::npos);
}

void testVmCompilesIndependentOptimizeTierAtThreshold() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "function twice(value) { return value * 2; } twice(3); twice(4); twice(5);");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(parser.diagnostics().empty());
  EXPECT(result.equals(minijs::Value(10.0)));

  const auto* feedback = vm.debugGlobalFunctionJitFeedback("twice");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.compileAttempts == 1);
  EXPECT(feedback->optimize.optimizedEntryCount == 1);
  EXPECT(feedback->optimize.code != nullptr);
  EXPECT(vm.debugGlobalFunctionOptimizedCode("twice") != nullptr);
  EXPECT(!feedback->optimize.code->lowered.blocks.empty());
  EXPECT(feedback->optimize.code->executableMemory != nullptr);
  EXPECT(feedback->optimize.code->entry != nullptr);
  EXPECT(feedback->optimize.code->deoptStates.size() == 1);
  const auto deoptVerified =
      minijs::verifyOptimizerDeoptMetadata(*feedback->optimize.code);
  EXPECT(deoptVerified.valid);
  const auto safepointVerified =
      minijs::verifyOptimizerSafepoints(*feedback->optimize.code);
  EXPECT(safepointVerified.valid);
  EXPECT(feedback->optimize.code->safepoints.size() == 1);
  EXPECT(feedback->optimize.code->safepoints[0].liveGcArguments.empty());
  const auto materialized = minijs::materializeOptimizerDeoptFrame(
      *feedback->optimize.code, 0, {minijs::Value(17.0)});
  EXPECT(materialized.valid);
  EXPECT(materialized.bytecodeOffset == 0);
  EXPECT(materialized.phase == minijs::DeoptPhase::Before);
  EXPECT(materialized.frameSlots.size() == 1);
  EXPECT(materialized.frameSlots[0].equals(minijs::Value(17.0)));
  const auto stats = vm.jitRuntimeStats();
  EXPECT(stats.optimizeCompiled >= 1);
  EXPECT(stats.optimizeAttempts >= 1);
  EXPECT(stats.optimizedEntries >= 1);
}

void testDeoptVerifierRejectsCorruptFrameRecipe() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "function identity(value) { return value; } identity(1); identity(2);");
  minijs::Compiler compiler;
  (void)vm.run(compiler.compileProgram(parser.parseProgram()));
  const auto* code = vm.debugGlobalFunctionOptimizedCode("identity");
  EXPECT(code != nullptr);
  minijs::OptimizedCode corrupt = *code;
  EXPECT(!corrupt.deoptStates.empty());
  ++corrupt.deoptStates[0].stackDepth;
  const auto verified = minijs::verifyOptimizerDeoptMetadata(corrupt);
  EXPECT(!verified.valid);
  EXPECT(verified.error.find("stack depth") != std::string::npos);
}

void testSafepointVerifierRejectsInvalidGcArgument() {
  minijs::OptimizedCode code;
  code.parameterCount = 1;
  code.entrySafepointId = 0;
  code.safepoints.push_back(minijs::OptimizedSafepoint{0, {1}});
  const auto verified = minijs::verifyOptimizerSafepoints(code);
  EXPECT(!verified.valid);
  EXPECT(verified.error.find("out of bounds") != std::string::npos);

  code.safepoints[0].liveGcArguments = {0, 0};
  const auto duplicate = minijs::verifyOptimizerSafepoints(code);
  EXPECT(!duplicate.valid);
  EXPECT(duplicate.error.find("duplicate") != std::string::npos);
}

void testOptimizeThresholdIsIndependentFromBaselineThreshold() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(2);
  minijs::Parser parser(
      "function twice(value) { return value * 2; } twice(3); twice(4);");
  minijs::Compiler compiler;
  (void)vm.run(compiler.compileProgram(parser.parseProgram()));

  const auto* feedback = vm.debugGlobalFunctionJitFeedback("twice");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Cold);
  EXPECT(feedback->optimize.compileAttempts == 0);
}

void testOptimizeFailureKeepsBaselineCompiled() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "let shared = 7; function readShared() { return shared; } "
      "readShared(); readShared();");
  minijs::Compiler compiler;
  (void)vm.run(compiler.compileProgram(parser.parseProgram()));

  const auto* feedback = vm.debugGlobalFunctionJitFeedback("readShared");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineCode != nullptr);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Failed);
  EXPECT(feedback->optimize.code == nullptr);
  EXPECT(feedback->optimize.compileAttempts == 1);
  EXPECT(!feedback->optimize.compileError.empty());
}

void testOptimizedNumberGuardFallsBackToBaseline() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "function identity(value) { return value; } "
      "identity(1); identity(2); identity(\"fallback\");");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(std::string("fallback"))));

  const auto* feedback = vm.debugGlobalFunctionJitFeedback("identity");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.guardFailures == 1);
  EXPECT(feedback->optimize.optimizedEntryCount == 0);
  EXPECT(feedback->baselineEntryCount == 2);
}

void testRepeatedGuardFailuresDisableOnlyOptimizeTier() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  vm.setOptimizeGuardFailureThreshold(2);
  minijs::Parser parser(
      "function identity(value) { return value; } "
      "identity(1); identity(2); identity(\"first\"); "
      "identity(\"second\"); identity(\"after-disable\");");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(std::string("after-disable"))));

  const auto* feedback = vm.debugGlobalFunctionJitFeedback("identity");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->baselineCode != nullptr);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Disabled);
  EXPECT(feedback->optimize.guardFailures == 2);
  EXPECT(feedback->optimize.optimizedEntryCount == 0);
  EXPECT(feedback->optimize.code != nullptr);
  EXPECT(feedback->baselineEntryCount == 4);
  const auto stats = vm.jitRuntimeStats();
  EXPECT(stats.optimizeDisabled >= 1);
  EXPECT(stats.optimizeGuardFailures >= 2);
}

minijs::Value runTieringProgram(std::string_view source, bool jitEnabled,
                               std::uint32_t optimizeThreshold) {
  minijs::Parser parser(source);
  minijs::Compiler compiler;
  minijs::VM vm;
  vm.setJitEnabled(jitEnabled);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(optimizeThreshold);
  return vm.run(compiler.compileProgram(parser.parseProgram()));
}

void testVmBaselineAndOptimizeProduceSameLoopResult() {
  constexpr std::string_view source =
      "function sumTo(n) { let i = 0; let sum = 0; "
      "while (i < n) { sum = sum + i; i = i + 1; } return sum; } "
      "sumTo(20); sumTo(20); sumTo(20);";
  const minijs::Value interpreted = runTieringProgram(
      source, false, std::numeric_limits<std::uint32_t>::max());
  const minijs::Value baseline = runTieringProgram(
      source, true, std::numeric_limits<std::uint32_t>::max());

  minijs::Parser parser(source);
  minijs::Compiler compiler;
  minijs::VM optimizedVm;
  optimizedVm.setJitEnabled(true);
  optimizedVm.setJitCallThreshold(1);
  optimizedVm.setOptimizeThreshold(1);
  const minijs::Value optimized =
      optimizedVm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(interpreted.equals(minijs::Value(190.0)));
  EXPECT(baseline.equals(interpreted));
  EXPECT(optimized.equals(interpreted));
  const auto* feedback = optimizedVm.debugGlobalFunctionJitFeedback("sumTo");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->baselineEntryCount == 1);
  EXPECT(feedback->optimize.optimizedEntryCount == 1);
  EXPECT(feedback->optimize.guardFailures == 0);
}

std::string runDivisionByZeroProgram(bool optimize) {
  constexpr std::string_view source =
      "function divide(a, b) { return a / b; } "
      "divide(8, 2); divide(9, 3); divide(1, 0);";
  minijs::Parser parser(source);
  minijs::Compiler compiler;
  minijs::VM vm;
  vm.setJitEnabled(optimize);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  try {
    (void)vm.run(compiler.compileProgram(parser.parseProgram()));
  } catch (const minijs::RuntimeError& error) {
    if (optimize) {
      const auto* feedback = vm.debugGlobalFunctionJitFeedback("divide");
      EXPECT(feedback != nullptr);
      EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
    }
    return error.what();
  }
  return "";
}

void testOptimizePreservesDivisionByZeroError() {
  const std::string interpreted = runDivisionByZeroProgram(false);
  const std::string optimized = runDivisionByZeroProgram(true);
  EXPECT(!interpreted.empty());
  EXPECT(optimized == interpreted);
  EXPECT(optimized.find("division by zero") != std::string::npos);
}

void testJitDisabledNeverAdvancesOptimizeState() {
  minijs::VM vm;
  vm.setJitEnabled(false);
  minijs::Parser parser(
      "function twice(value) { return value * 2; } twice(1); twice(2); twice(3);");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(6.0)));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("twice");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Cold);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Cold);
  EXPECT(feedback->optimize.compileAttempts == 0);
  EXPECT(feedback->optimize.optimizedEntryCount == 0);
}

void testMonomorphicPropertyLoadEntersOptimizeTier() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "let object = {x: 41}; function readX(value) { return value.x; } "
      "readX(object); readX(object); readX(object);");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(41.0)));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("readX");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.optimizedEntryCount == 1);
  EXPECT(feedback->optimize.guardFailures == 0);
  EXPECT(feedback->optimize.code->propertyAccess.kind ==
         minijs::OptimizedPropertyAccessKind::Load);
  EXPECT(feedback->optimize.code->propertyAccess.shape != nullptr);
  const auto& safepoint = feedback->optimize.code->safepoints[0];
  EXPECT(safepoint.liveGcArguments.size() == 1);
  EXPECT(safepoint.liveGcArguments[0] == 0);
}

void testOptimizedSafepointKeepsSpilledObjectAliveDuringGc() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  vm.debugSetOptimizeSafepointStressEnabled(true);
  minijs::Parser parser(
      "function readX(value) { return value.x; } "
      "readX({x: 1}); readX({x: 2}); readX({x: 41});");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(41.0)));
  EXPECT(vm.debugOptimizeSafepointStressCount() == 1);
  EXPECT(vm.debugOptimizeSafepointStressRootsSurvived());
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("readX");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->optimize.optimizedEntryCount == 1);
  EXPECT(feedback->optimize.guardFailures == 0);
}

void testMonomorphicPropertyStoreEntersOptimizeTier() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "let object = {x: 1}; function writeX(target, value) { return target.x = value; } "
      "writeX(object, 2); writeX(object, 3); writeX(object, 4); object.x;");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(4.0)));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("writeX");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.optimizedEntryCount == 1);
  EXPECT(feedback->optimize.code->propertyAccess.kind ==
         minijs::OptimizedPropertyAccessKind::Store);
}

void testPropertyShapeGuardFallsBackBeforeAccess() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "let first = {x: 1}; let second = {y: 0, x: 9}; "
      "function readX(value) { return value.x; } "
      "readX(first); readX(first); readX(second);");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(9.0)));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("readX");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.guardFailures == 1);
  EXPECT(feedback->optimize.optimizedEntryCount == 0);
}

void testMonomorphicMethodCallEntersOptimizeTier() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "class Counter { get() { return 7; } } let counter = Counter(); "
      "function callGet(receiver) { return receiver.get(); } "
      "callGet(counter); callGet(counter); callGet(counter);");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(7.0)));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("callGet");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.optimizedEntryCount == 1);
  EXPECT(feedback->optimize.guardFailures == 0);
  EXPECT(feedback->optimize.code->methodCall.valid());
  EXPECT(feedback->optimize.code->methodCall.argumentIndices.empty());
  const auto& safepoint = feedback->optimize.code->safepoints[0];
  EXPECT(safepoint.liveGcArguments.size() == 1);
  EXPECT(safepoint.liveGcArguments[0] == 0);
}

void testMonomorphicMethodCallForwardsArguments() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "class Adder { add(value) { return value + 1; } } let adder = Adder(); "
      "function callAdd(receiver, value) { return receiver.add(value); } "
      "callAdd(adder, 1); callAdd(adder, 2); callAdd(adder, 3);");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(4.0)));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("callAdd");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.optimizedEntryCount == 1);
  EXPECT(feedback->optimize.code->methodCall.valid());
  EXPECT(feedback->optimize.code->methodCall.argumentIndices.size() == 1);
  EXPECT(feedback->optimize.code->methodCall.argumentIndices[0] == 1);
}

void testMethodClassGuardFallsBackBeforeCall() {
  minijs::VM vm;
  vm.setJitEnabled(true);
  vm.setJitCallThreshold(1);
  vm.setOptimizeThreshold(1);
  minijs::Parser parser(
      "class First { get() { return 1; } } class Second { get() { return 9; } } "
      "let first = First(); let second = Second(); "
      "function callGet(receiver) { return receiver.get(); } "
      "callGet(first); callGet(first); callGet(second);");
  minijs::Compiler compiler;
  const minijs::Value result = vm.run(compiler.compileProgram(parser.parseProgram()));
  EXPECT(result.equals(minijs::Value(9.0)));
  const auto* feedback = vm.debugGlobalFunctionJitFeedback("callGet");
  EXPECT(feedback != nullptr);
  EXPECT(feedback->state == minijs::JitState::Compiled);
  EXPECT(feedback->optimize.state == minijs::OptimizeState::Compiled);
  EXPECT(feedback->optimize.guardFailures == 1);
  EXPECT(feedback->optimize.optimizedEntryCount == 0);
}

}  // namespace

void runOptimizerIrTests() {
  testStraightLineCfg();
  testDiamondCfg();
  testLoopCfgHasBackedge();
  testInvalidBytecodeIsRejected();
  testStackUnderflowIsRejected();
  testMergeWithDifferentStackDepthIsRejected();
  testUnsupportedOpcodeIsRejectedByOptimizerAnalysis();
  testStraightLineBytecodeBecomesValueIr();
  testLoopLocalCreatesLoopCarriedPhi();
  testIrInterpreterMatchesVmForArithmeticFunction();
  testIrInterpreterMatchesVmForLoopFunction();
  testIrInterpreterRejectsWrongArgumentCount();
  testTypeInferenceFindsNumericArithmetic();
  testTypeInferenceStabilizesLoopPhi();
  testTypeInferenceJoinsDifferentPhiTypesToUnknown();
  testTypeInferenceDoesNotCallStringAddNumeric();
  testTypeInferencePreservesUnknownParameter();
  testNumericArithmeticIsOptimizerEligible();
  testNumericLoopIsOptimizerEligible();
  testUnknownParameterIsNotOptimizerEligible();
  testStringAddIsNotOptimizerEligible();
  testMixedPhiIsNotOptimizerEligible();
  testVmProfilesStableNumberParameters();
  testVmProfilesPolymorphicParameters();
  testNumberGuardsAcceptProfiledShapeAndRejectWrongType();
  testLoopEntryNumberGuardPrecedesComputation();
  testOptimizerPassesFoldNumericConstants();
  testOptimizerPassesFoldConstantBranch();
  testOptimizerPassesRemoveRedundantPhi();
  testOptimizerPassesRemoveDeadPureComputation();
  testNumberLoweringUsesUnboxedArithmeticAndAbiBoxes();
  testNumberLoweringPreservesLoopPhis();
  testNumberLoweringRetainsEntryGuard();
  testLoweredVerifierRejectsRepresentationMismatch();
  testVmCompilesIndependentOptimizeTierAtThreshold();
  testDeoptVerifierRejectsCorruptFrameRecipe();
  testSafepointVerifierRejectsInvalidGcArgument();
  testOptimizeThresholdIsIndependentFromBaselineThreshold();
  testOptimizeFailureKeepsBaselineCompiled();
  testOptimizedNumberGuardFallsBackToBaseline();
  testRepeatedGuardFailuresDisableOnlyOptimizeTier();
  testVmBaselineAndOptimizeProduceSameLoopResult();
  testOptimizePreservesDivisionByZeroError();
  testJitDisabledNeverAdvancesOptimizeState();
  testMonomorphicPropertyLoadEntersOptimizeTier();
  testOptimizedSafepointKeepsSpilledObjectAliveDuringGc();
  testMonomorphicPropertyStoreEntersOptimizeTier();
  testPropertyShapeGuardFallsBackBeforeAccess();
  testMonomorphicMethodCallEntersOptimizeTier();
  testMonomorphicMethodCallForwardsArguments();
  testMethodClassGuardFallsBackBeforeCall();
}
