#pragma once

#include <string>

#include "minijs/optimized_code.h"

namespace minijs {

struct OptimizerSafepointVerificationResult {
  bool valid = false;
  std::string error;
};

OptimizerSafepointVerificationResult verifyOptimizerSafepoints(
    const OptimizedCode& code);

const OptimizedSafepoint* findOptimizerSafepoint(
    const OptimizedCode& code, OptimizerSafepointId id);

}  // namespace minijs
