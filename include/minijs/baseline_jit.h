#pragma once

#include "minijs/baseline_compiler.h"
#include "minijs/bytecode_function.h"

namespace minijs {

BaselineCompileResult compileBaseline(const BytecodeFunction& function);

}  // namespace minijs
