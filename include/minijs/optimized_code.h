#pragma once

#include <limits>
#include <memory>

#include "minijs/executable_memory.h"
#include "minijs/optimized_frame.h"
#include "minijs/optimizer_ir.h"

namespace minijs {

struct ObjShape;
struct ObjClass;
struct ObjClosure;

enum class DeoptPhase : std::uint8_t {
  Before,
  After,
};

enum class DeoptLocationKind : std::uint8_t {
  Argument,
  NumberConstant,
  BooleanConstant,
};

struct DeoptValueLocation {
  DeoptLocationKind kind = DeoptLocationKind::Argument;
  std::uint32_t index = 0;
  double number = 0.0;
  bool boolean = false;
};

struct DeoptState {
  OptimizerDeoptId id = InvalidOptimizerDeopt;
  std::size_t bytecodeOffset = 0;
  DeoptPhase phase = DeoptPhase::Before;
  std::size_t stackDepth = 0;
  std::vector<DeoptValueLocation> frameSlots;
};

enum class OptimizedPropertyAccessKind : std::uint8_t {
  None,
  Load,
  Store,
};

struct OptimizedPropertyAccess {
  OptimizedPropertyAccessKind kind = OptimizedPropertyAccessKind::None;
  ObjShape* shape = nullptr;
  std::size_t slot = 0;
  std::uint32_t objectArgument = 0;
  std::uint32_t valueArgument = 0;
  OptimizerDeoptId deoptId = InvalidOptimizerDeopt;
};

struct OptimizedMethodCall {
  ObjClass* klass = nullptr;
  ObjClosure* method = nullptr;
  std::uint32_t receiverArgument = 0;
  std::vector<std::uint32_t> argumentIndices;
  OptimizerDeoptId deoptId = InvalidOptimizerDeopt;

  bool valid() const { return klass != nullptr && method != nullptr; }
};

using OptimizerSafepointId = std::uint32_t;
constexpr OptimizerSafepointId InvalidOptimizerSafepoint =
    std::numeric_limits<OptimizerSafepointId>::max();

struct OptimizedSafepoint {
  OptimizerSafepointId id = InvalidOptimizerSafepoint;
  std::vector<std::uint32_t> liveGcArguments;
};

// Optimize tier code object. Until native codegen lands, lowered IR is the
// executable-independent payload and the high-level IR remains available for
// diagnostics and future deopt metadata construction.
struct OptimizedCode {
  std::size_t parameterCount = 0;
  OptimizerIrFunction ir;
  OptimizerLoweredFunction lowered;
  std::vector<DeoptState> deoptStates;
  OptimizedPropertyAccess propertyAccess;
  OptimizedMethodCall methodCall;
  std::vector<OptimizedSafepoint> safepoints;
  OptimizerSafepointId entrySafepointId = InvalidOptimizerSafepoint;
  std::shared_ptr<ExecutableMemory> executableMemory;
  OptimizedEntry entry = nullptr;
};

}  // namespace minijs
