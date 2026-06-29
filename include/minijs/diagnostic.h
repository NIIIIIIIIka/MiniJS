#pragma once

#include <string>
#include <utility>

#include "minijs/source_location.h"

namespace minijs {

// 编译前端产生的诊断信息，目前用于词法和语法错误。
struct Diagnostic {
  SourceLocation location;
  std::string message;

  Diagnostic(SourceLocation sourceLocation, std::string diagnosticMessage)
      : location(sourceLocation), message(std::move(diagnosticMessage)) {}
};

}  // namespace minijs
