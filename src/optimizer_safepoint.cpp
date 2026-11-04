#include "minijs/optimizer_safepoint.h"

#include <vector>

namespace minijs {

OptimizerSafepointVerificationResult verifyOptimizerSafepoints(
    const OptimizedCode& code) {
  OptimizerSafepointVerificationResult result;
  if (code.entrySafepointId >= code.safepoints.size()) {
    result.error = "optimizer entry has no safepoint";
    return result;
  }
  for (std::size_t index = 0; index < code.safepoints.size(); ++index) {
    const OptimizedSafepoint& safepoint = code.safepoints[index];
    if (safepoint.id != index) {
      result.error = "optimizer safepoint id is invalid";
      return result;
    }
    std::vector<bool> seen(code.parameterCount, false);
    for (const std::uint32_t argument : safepoint.liveGcArguments) {
      if (argument >= code.parameterCount) {
        result.error = "optimizer safepoint argument is out of bounds";
        return result;
      }
      if (seen[argument]) {
        result.error = "optimizer safepoint contains a duplicate argument";
        return result;
      }
      seen[argument] = true;
    }
  }
  result.valid = true;
  return result;
}

const OptimizedSafepoint* findOptimizerSafepoint(
    const OptimizedCode& code, OptimizerSafepointId id) {
  if (id >= code.safepoints.size() || code.safepoints[id].id != id) return nullptr;
  return &code.safepoints[id];
}

}  // namespace minijs
