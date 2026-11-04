#pragma once

#include <memory>
#include <string>

namespace minijs {

struct BytecodeFunction;
struct OptimizedCode;

struct OptimizerCompileResult {
  std::shared_ptr<OptimizedCode> code;
  std::string error;

  bool succeeded() const { return code != nullptr; }
};

OptimizerCompileResult compileOptimized(const BytecodeFunction& function);

}  // namespace minijs
