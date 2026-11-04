#pragma once

#include <string>
#include <vector>

#include "minijs/optimized_code.h"

namespace minijs {

struct OptimizerDeoptVerificationResult {
  bool valid = false;
  std::string error;
};

OptimizerDeoptVerificationResult verifyOptimizerDeoptMetadata(
    const OptimizedCode& code);

struct OptimizerDeoptMaterializationResult {
  bool valid = false;
  std::size_t bytecodeOffset = 0;
  DeoptPhase phase = DeoptPhase::Before;
  std::vector<Value> frameSlots;
  std::string error;
};

OptimizerDeoptMaterializationResult materializeOptimizerDeoptFrame(
    const OptimizedCode& code, OptimizerDeoptId deoptId,
    const std::vector<Value>& arguments);

}  // namespace minijs
