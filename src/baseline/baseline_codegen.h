#pragma once

#include "minijs/baseline_compiler.h"

namespace minijs::detail {

// BaselineCompiler 的内部 native codegen 入口。
// 该接口只在 src 内部使用，用于隔离公开编译器门面与平台相关实现。
BaselineCompileResult compileBaselineNative(const BytecodeFunction& function);

}  // namespace minijs::detail
