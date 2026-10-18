#pragma once

#include <memory>
#include <string>

#include "minijs/baseline_code.h"
#include "minijs/bytecode_function.h"

namespace minijs {

struct BaselineCompileResult {
  std::shared_ptr<BaselineCode> code;
  std::string error;

  bool succeeded() const { return code != nullptr; }
};

class BaselineCompiler {
 public:
  // 对外保持稳定的编译入口；bytecode 校验、平台 codegen 和 ABI 发射细节
  // 均由 src 内部模块负责。不支持的平台或指令通过 error 返回。
  BaselineCompileResult compile(const BytecodeFunction& function) const;
};

}  // namespace minijs
