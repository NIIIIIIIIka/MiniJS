#include "minijs/baseline_compiler.h"

#include "baseline_codegen.h"

namespace minijs {

BaselineCompileResult BaselineCompiler::compile(const BytecodeFunction& function) const {
  return detail::compileBaselineNative(function);
}

}  // namespace minijs
