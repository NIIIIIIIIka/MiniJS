#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "minijs/bytecode_decoder.h"
#include "minijs/value.h"

namespace minijs {

using OptimizerBlockId = std::uint32_t;

struct OptimizerBasicBlock {
  OptimizerBlockId id = 0;
  std::size_t bytecodeStart = 0;
  std::size_t bytecodeEnd = 0;
  std::vector<DecodedInstruction> instructions;
  std::vector<OptimizerBlockId> predecessors;
  std::vector<OptimizerBlockId> successors;
};

struct OptimizerControlFlowGraph {
  std::vector<OptimizerBasicBlock> blocks;
  OptimizerBlockId entry = 0;
};

struct OptimizerCfgResult {
  bool valid = false;
  OptimizerControlFlowGraph graph;
  std::string error;
};

// Builds a structural control-flow graph from verified bytecode. This layer is
// deliberately independent from BaselineCode so later SSA construction can
// reject unsupported semantics without weakening bytecode validation.
OptimizerCfgResult buildOptimizerControlFlowGraph(const Chunk& chunk);

struct OptimizerCfgVerificationResult {
  bool valid = false;
  std::string error;
};

OptimizerCfgVerificationResult verifyOptimizerControlFlowGraph(
    const OptimizerControlFlowGraph& graph, std::size_t bytecodeSize);

struct OptimizerStackAnalysisResult {
  bool valid = false;
  // Indexed by OptimizerBlockId. Unreachable blocks use unreachableDepth.
  std::vector<std::size_t> entryDepths;
  std::vector<std::size_t> exitDepths;
  std::string error;

  static constexpr std::size_t unreachableDepth = static_cast<std::size_t>(-1);
};

// Computes stack heights without executing values. initialDepth is the number
// of live frame slots on function entry (parameters for a normal function,
// receiver plus parameters for a method). Only the numeric SSA prototype's
// opcode subset is accepted. Every reachable merge must have one stack shape.
OptimizerStackAnalysisResult analyzeOptimizerStackDepths(
    const OptimizerControlFlowGraph& graph, std::size_t initialDepth);

using OptimizerValueId = std::uint32_t;
inline constexpr OptimizerValueId InvalidOptimizerValue =
    std::numeric_limits<OptimizerValueId>::max();
using OptimizerDeoptId = std::uint32_t;
inline constexpr OptimizerDeoptId InvalidOptimizerDeopt =
    std::numeric_limits<OptimizerDeoptId>::max();

enum class OptimizerIrOpcode : std::uint8_t {
  Parameter,
  Constant,
  Phi,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Negate,
  Equal,
  Greater,
  Less,
  Not,
  GuardNumber,
  Branch,
  Jump,
  Return,
};

struct OptimizerIrInstruction {
  OptimizerIrOpcode opcode = OptimizerIrOpcode::Constant;
  OptimizerValueId result = InvalidOptimizerValue;
  std::vector<OptimizerValueId> inputs;
  std::uint32_t immediate = 0;
  bool hasLiteral = false;
  Value literal;
  std::size_t bytecodeOffset = 0;
  OptimizerBlockId trueTarget = 0;
  OptimizerBlockId falseTarget = 0;
};

struct OptimizerIrBlock {
  OptimizerBlockId id = 0;
  std::vector<OptimizerBlockId> predecessors;
  std::vector<OptimizerBlockId> successors;
  std::vector<OptimizerValueId> entryValues;
  std::vector<OptimizerValueId> exitValues;
  std::vector<OptimizerIrInstruction> instructions;
};

struct OptimizerIrFunction {
  std::vector<OptimizerIrBlock> blocks;
  OptimizerBlockId entry = 0;
  std::size_t parameterCount = 0;
  OptimizerValueId valueCount = 0;
};

struct OptimizerIrResult {
  bool valid = false;
  OptimizerIrFunction function;
  std::string error;
};

// Converts the supported bytecode subset into an explicit value graph. Block
// entry slots are represented by Phi nodes, including loop-carried locals.
// Redundant one-input Phi nodes are intentional in this first correctness pass.
OptimizerIrResult buildOptimizerIr(const Chunk& chunk, std::size_t initialDepth);

struct OptimizerIrVerificationResult {
  bool valid = false;
  std::string error;
};

OptimizerIrVerificationResult verifyOptimizerIr(const OptimizerIrFunction& function);

struct OptimizerIrExecutionResult {
  bool completed = false;
  bool guardFailed = false;
  Value value;
  std::string error;
};

// Executes IR as a correctness oracle before optimized machine code exists.
// The step limit turns malformed or accidentally infinite graphs into a
// deterministic failure instead of hanging tests.
OptimizerIrExecutionResult executeOptimizerIr(
    const OptimizerIrFunction& function, const Chunk& constants,
    const std::vector<Value>& arguments, std::size_t stepLimit = 1000000);

enum class OptimizerType : std::uint8_t {
  Bottom,
  Number,
  Boolean,
  Null,
  Undefined,
  String,
  Object,
  Unknown,
};

const char* optimizerTypeName(OptimizerType type);

struct OptimizerTypeAnalysisResult {
  bool valid = false;
  // Indexed by OptimizerValueId.
  std::vector<OptimizerType> types;
  std::string error;
};

// Computes a fixed point over SSA dependencies. Parameter assumptions come
// from runtime feedback in production and are supplied explicitly in tests.
OptimizerTypeAnalysisResult inferOptimizerTypes(
    const OptimizerIrFunction& function, const Chunk& constants,
    const std::vector<OptimizerType>& parameterTypes);

enum class OptimizerEligibilityFailure : std::uint8_t {
  None,
  InvalidIr,
  TypeCountMismatch,
  NonNumberParameter,
  UnsupportedConstant,
  UnsupportedPhiType,
  NonNumericOperation,
  InvalidConditionType,
  UnsupportedReturnType,
};

const char* optimizerEligibilityFailureName(OptimizerEligibilityFailure failure);

struct OptimizerEligibilityResult {
  bool eligible = false;
  OptimizerEligibilityFailure failure = OptimizerEligibilityFailure::None;
  std::size_t bytecodeOffset = 0;
  OptimizerValueId value = InvalidOptimizerValue;
  std::string error;
};

// Enforces the first Optimize JIT boundary: number parameters, primitive
// numeric/boolean values, no generic Add semantics, and number/boolean return.
OptimizerEligibilityResult analyzeOptimizerEligibility(
    const OptimizerIrFunction& function, const Chunk& constants,
    const OptimizerTypeAnalysisResult& types);

struct OptimizerGuardInsertionResult {
  bool valid = false;
  OptimizerIrFunction function;
  std::size_t guardCount = 0;
  std::string error;
};

// Inserts entry guards for profiled Number parameters after entry Phi nodes
// and before observable computation.
OptimizerGuardInsertionResult insertOptimizerNumberGuards(
    const OptimizerIrFunction& function, const OptimizerEligibilityResult& eligibility);

struct OptimizerPassStats {
  std::size_t phisRemoved = 0;
  std::size_t constantsFolded = 0;
  std::size_t branchesFolded = 0;
  std::size_t deadInstructionsRemoved = 0;
};

struct OptimizerPassResult {
  bool valid = false;
  OptimizerIrFunction function;
  OptimizerPassStats stats;
  std::string error;
};

OptimizerPassResult optimizeOptimizerIr(const OptimizerIrFunction& function,
                                        const Chunk& constants);

// Representation-selected IR consumed by the future native backend. Tagged
// values exist only at the ABI boundary; arithmetic and control flow operate
// on unboxed Number/Boolean values.
enum class OptimizerRepresentation : std::uint8_t {
  Tagged,
  Number,
  Boolean,
};

enum class OptimizerLoweredOpcode : std::uint8_t {
  Parameter,
  GuardNumber,
  UnboxNumber,
  NumberConstant,
  BooleanConstant,
  PhiNumber,
  PhiBoolean,
  AddNumber,
  SubNumber,
  MulNumber,
  DivNumber,
  ModNumber,
  NegateNumber,
  EqualNumber,
  EqualBoolean,
  GreaterNumber,
  LessNumber,
  NotNumber,
  NotBoolean,
  BranchNumber,
  BranchBoolean,
  Jump,
  BoxNumber,
  BoxBoolean,
  Return,
};

struct OptimizerLoweredInstruction {
  OptimizerLoweredOpcode opcode = OptimizerLoweredOpcode::NumberConstant;
  OptimizerValueId result = InvalidOptimizerValue;
  std::vector<OptimizerValueId> inputs;
  std::uint32_t immediate = 0;
  double numberImmediate = 0.0;
  bool booleanImmediate = false;
  std::size_t bytecodeOffset = 0;
  OptimizerBlockId trueTarget = 0;
  OptimizerBlockId falseTarget = 0;
  OptimizerDeoptId deoptId = InvalidOptimizerDeopt;
};

struct OptimizerLoweredBlock {
  OptimizerBlockId id = 0;
  std::vector<OptimizerBlockId> predecessors;
  std::vector<OptimizerBlockId> successors;
  std::vector<OptimizerLoweredInstruction> instructions;
};

struct OptimizerLoweredFunction {
  std::vector<OptimizerLoweredBlock> blocks;
  OptimizerBlockId entry = 0;
  std::size_t parameterCount = 0;
  OptimizerValueId valueCount = 0;
  // Indexed by lowered OptimizerValueId.
  std::vector<OptimizerRepresentation> representations;
};

struct OptimizerLoweringResult {
  bool valid = false;
  OptimizerLoweredFunction function;
  std::string error;
};

// Lowers an eligible, guarded value IR into explicit unboxed operations.
OptimizerLoweringResult lowerOptimizerNumberIr(
    const OptimizerIrFunction& function, const Chunk& constants,
    const OptimizerTypeAnalysisResult& types);

struct OptimizerLoweredVerificationResult {
  bool valid = false;
  std::string error;
};

OptimizerLoweredVerificationResult verifyOptimizerLoweredIr(
    const OptimizerLoweredFunction& function);

struct OptimizerLoweredExecutionResult {
  bool completed = false;
  bool guardFailed = false;
  Value value;
  OptimizerDeoptId deoptId = InvalidOptimizerDeopt;
  std::string error;
};

// Executes lowered IR as a representation and lowering correctness oracle.
OptimizerLoweredExecutionResult executeOptimizerLoweredIr(
    const OptimizerLoweredFunction& function,
    const std::vector<Value>& arguments, std::size_t stepLimit = 1000000);

}  // namespace minijs
