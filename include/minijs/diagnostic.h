#pragma once

#include <string>
#include <utility>

#include "minijs/source_location.h"

namespace minijs {
struct Diagnostic {
  SourceLocation location;
  std::string message;

  Diagnostic(SourceLocation sourceLocation, std::string diagnosticMessage)
      : location(sourceLocation), message(std::move(diagnosticMessage)) {}
};
}  // namespace minijs
