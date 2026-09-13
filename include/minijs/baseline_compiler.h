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
  // 生成第一版 native baseline code。当前 backend 只覆盖一小组 Opcode；
  // 不支持的指令通过 error 返回，让外层 compileBaseline() 决定是否回退。
  BaselineCompileResult compile(const BytecodeFunction& function) const;
};

}  // namespace minijs
