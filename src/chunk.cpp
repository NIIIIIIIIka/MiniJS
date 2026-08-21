#include "minijs/chunk.h"

#include <utility>

#include "minijs/runtime_error.h"

namespace minijs {

Chunk::Chunk(const Chunk& other) : code_(other.code_), constants_(other.constants_) {}

Chunk& Chunk::operator=(const Chunk& other) {
  if (this == &other) {
    return *this;
  }

  code_ = other.code_;
  constants_ = other.constants_;
  clearInlineCaches();
  return *this;
}

Chunk::Chunk(Chunk&& other) noexcept
    : code_(std::move(other.code_)), constants_(std::move(other.constants_)) {
  other.clearInlineCaches();
}

Chunk& Chunk::operator=(Chunk&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  code_ = std::move(other.code_);
  constants_ = std::move(other.constants_);
  clearInlineCaches();
  other.clearInlineCaches();
  return *this;
}

void Chunk::writeOpcode(Opcode opcode) { code_.push_back(static_cast<std::uint8_t>(opcode)); }

void Chunk::writeByte(std::uint8_t byte) { code_.push_back(byte); }

std::uint8_t Chunk::readByte(std::size_t offset) const {
  if (offset >= code_.size()) {
    throw RuntimeError("bytecode read out of bounds");
  }
  return code_[offset];
}

std::size_t Chunk::addConstant(Value value) {
  constants_.push_back(std::move(value));
  return constants_.size() - 1;
}

const Value& Chunk::constant(std::size_t index) const {
  if (index >= constants_.size()) {
    throw RuntimeError("constant index out of bounds");
  }
  return constants_[index];
}

const std::vector<std::uint8_t>& Chunk::code() const { return code_; }

const std::vector<Value>& Chunk::constants() const { return constants_; }

PropertyInlineCache& Chunk::propertyInlineCache(std::size_t opcodeOffset) const {
  return propertyInlineCaches_[opcodeOffset];
}

void Chunk::clearInlineCaches() const { propertyInlineCaches_.clear(); }

std::size_t Chunk::count() const { return code_.size(); }

void Chunk::patchByte(std::size_t offset, std::uint8_t byte) {
  if (offset >= code_.size()) {
    throw RuntimeError("bytecode patch out of bounds");
  }
  code_[offset] = byte;
}
}  // namespace minijs
