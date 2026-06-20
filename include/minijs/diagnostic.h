#pragma once

#include "minijs/source_location.h"

#include <string>
#include <utility>

namespace minijs
{
struct Diagnostic
{
    SourceLocation location;
    std::string message;

    Diagnostic(SourceLocation sourceLocation, std::string diagnosticMessage)
        : location(sourceLocation), message(std::move(diagnosticMessage))
    {
    }
};
}
