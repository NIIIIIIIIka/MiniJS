#pragma once

#include <cstddef>

namespace minijs::detail {

enum class BinaryOperation {
  Add,
  Subtract,
  Multiply,
  Divide,
};

enum class NativeBranchKind {
  Unconditional,
  Zero,
  NotOne,
};

struct BranchPatch {
  std::size_t offset = 0;
  NativeBranchKind kind = NativeBranchKind::Unconditional;

  BranchPatch() = default;
  BranchPatch(std::size_t offset, bool conditional)
      : offset(offset),
        kind(conditional ? NativeBranchKind::Zero : NativeBranchKind::Unconditional) {}
  BranchPatch(std::size_t offset, NativeBranchKind kind) : offset(offset), kind(kind) {}
};

struct NativeBranchPatch {
  std::size_t offset = 0;
  std::size_t bytecodeTarget = 0;
  NativeBranchKind kind = NativeBranchKind::Unconditional;
};

struct BinaryNumberGuardPatches {
  std::size_t notEnoughValues = 0;
  std::size_t leftNonNumber = 0;
  std::size_t rightNonNumber = 0;
  std::size_t rightZero = 0;
  bool hasRightZero = false;
};

}  // namespace minijs::detail
