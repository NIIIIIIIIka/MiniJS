#pragma once

#include <cstddef>
#include <ostream>
#include <string>

#include "minijs/chunk.h"

namespace minijs {

std::string disassembleChunk(const Chunk& chunk);

std::size_t disassembleInstruction(const Chunk& chunk, std::size_t offset, std::ostream& output);

}  // namespace minijs
