#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "minijs/value.h"
#include "minijs/optimizer_ir.h"

namespace minijs {

struct OptimizedCode;
struct ObjClosure;

constexpr std::uint32_t InvalidOptimizerFrameSafepoint =
    std::numeric_limits<std::uint32_t>::max();

struct OptimizedFrame {
  const OptimizedCode* code = nullptr;
  const Value* arguments = nullptr;
  std::size_t argumentCount = 0;
  Value result;
  bool completed = false;
  bool guardFailed = false;
  OptimizerDeoptId deoptId = InvalidOptimizerDeopt;
  bool failed = false;
  bool directCall = false;
  ObjClosure* directCallTarget = nullptr;
  std::uint32_t safepointId = InvalidOptimizerFrameSafepoint;
  void* safepointHookContext = nullptr;
  void (*safepointHook)(void*) = nullptr;
  std::string errorMessage;
};

using OptimizedEntry = void (*)(OptimizedFrame*);

}  // namespace minijs

extern "C" bool minijsExecuteOptimizedLowered(minijs::OptimizedFrame* frame) noexcept;
