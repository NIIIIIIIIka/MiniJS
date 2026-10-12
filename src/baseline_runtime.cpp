#include "minijs/baseline_runtime.h"

#include <exception>
#include <chrono>
#include <string>
#include <string_view>
#include <utility>

#include "minijs/runtime_error.h"
#include "minijs/vm.h"

namespace {

// 只在显式开启时读时钟；异常退出也计入一次 helper 调用。
class HelperProfileScope {
 public:
  HelperProfileScope(minijs::VM& vm, const char* name) noexcept
      : vm_(vm.baselineHelperProfilingEnabled() ? &vm : nullptr), name_(name) {
    if (vm_ != nullptr) {
      start_ = std::chrono::steady_clock::now();
    }
  }
  ~HelperProfileScope() noexcept {
    if (vm_ != nullptr) {
      const auto elapsed = std::chrono::steady_clock::now() - start_;
      vm_->recordBaselineHelperProfile(name_, static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count()));
    }
  }

 private:
  minijs::VM* vm_;
  const char* name_;
  std::chrono::steady_clock::time_point start_;
};

bool frameCanRun(minijs::BaselineFrame* frame) {
  if (frame == nullptr || frame->vm == nullptr) {
    return false;
  }
  if (frame->failed || frame->completed) {
    frame->failed = true;
    return false;
  }
  return true;
}

bool markFailed(minijs::BaselineFrame* frame) {
  if (frame != nullptr) {
    frame->failed = true;
  }
  return false;
}

bool markFailed(minijs::BaselineFrame* frame, std::string message) {
  if (frame != nullptr) {
    frame->failed = true;
    frame->errorMessage = std::move(message);
  }
  return false;
}

std::string runtimeMessage(const minijs::RuntimeError& error) {
  constexpr std::string_view prefix = "RuntimeError: ";
  const std::string_view message = error.what();
  if (message.substr(0, prefix.size()) == prefix) {
    return std::string(message.substr(prefix.size()));
  }
  return std::string(message);
}

bool markFailedFromException(minijs::BaselineFrame* frame, const minijs::RuntimeError& error) {
  return markFailed(frame, runtimeMessage(error));
}

bool markFailedFromException(minijs::BaselineFrame* frame, const std::exception& error) {
  return markFailed(frame, error.what());
}

}  // namespace

extern "C" bool minijsBaselinePush(minijs::BaselineFrame* frame, const minijs::Value* value) {
  if (!frameCanRun(frame) || value == nullptr) {
    return markFailed(frame);
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimePush(*frame, *value);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselinePushConstant(minijs::BaselineFrame* frame,
                                            std::uint32_t constantIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimePushConstant(*frame, constantIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGetLocal(minijs::BaselineFrame* frame, std::uint32_t slot) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGetLocal(*frame, slot);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselinePushNumberSlot(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }
  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimePushNumberSlot(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGetLocalAndPushNumberSlot(minijs::BaselineFrame* frame,
                                                       std::uint32_t slot) {
  if (!frameCanRun(frame)) {
    return false;
  }
  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGetLocalAndPushNumberSlot(*frame, slot);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSetLocal(minijs::BaselineFrame* frame, std::uint32_t slot) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSetLocal(*frame, slot);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineCopyLocal(minijs::BaselineFrame* frame, std::uint32_t sourceSlot,
                                       std::uint32_t targetSlot) {
  if (!frameCanRun(frame)) return false;
  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeCopyLocal(*frame, sourceSlot, targetSlot);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselinePop(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimePop(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineDefineGlobal(minijs::BaselineFrame* frame,
                                            std::uint32_t nameIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeDefineGlobal(*frame, nameIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGetGlobal(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGetGlobal(*frame, nameIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSetGlobal(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSetGlobal(*frame, nameIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineArray(minijs::BaselineFrame* frame, std::uint32_t count) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeArray(*frame, count);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGetIndex(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGetIndex(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSetIndex(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSetIndex(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineObject(minijs::BaselineFrame* frame, std::uint32_t namesIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeObject(*frame, namesIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGetProperty(minijs::BaselineFrame* frame,
                                           std::uint32_t nameIndex,
                                           std::uint32_t feedbackSlotIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGetProperty(*frame, nameIndex, feedbackSlotIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSetProperty(minijs::BaselineFrame* frame,
                                           std::uint32_t nameIndex,
                                           std::uint32_t feedbackSlotIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSetProperty(*frame, nameIndex, feedbackSlotIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineClass(minijs::BaselineFrame* frame, std::uint32_t nameIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeClass(*frame, nameIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineMethod(minijs::BaselineFrame* frame, std::uint32_t nameIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeMethod(*frame, nameIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineStaticMethod(minijs::BaselineFrame* frame,
                                            std::uint32_t nameIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeStaticMethod(*frame, nameIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineInherit(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeInherit(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGetUpvalue(minijs::BaselineFrame* frame, std::uint32_t slot) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGetUpvalue(*frame, slot);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSetUpvalue(minijs::BaselineFrame* frame, std::uint32_t slot) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSetUpvalue(*frame, slot);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineCloseUpvalue(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeCloseUpvalue(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGetCurrentClosure(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGetCurrentClosure(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineClosure(minijs::BaselineFrame* frame,
                                       std::uint32_t functionIndex,
                                       std::uint32_t bytecodeOffset) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeClosure(*frame, functionIndex, bytecodeOffset);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineCall(minijs::BaselineFrame* frame, std::uint32_t argCount) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeCall(*frame, argCount);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineMethodCall(minijs::BaselineFrame* frame,
                                          std::uint32_t nameIndex,
                                          std::uint32_t argCount,
                                          std::uint32_t feedbackSlotIndex) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeMethodCall(*frame, nameIndex, argCount, feedbackSlotIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSuperCall(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex,
                                         std::uint32_t argCount) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSuperCall(*frame, nameIndex, argCount);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineAdd(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeAdd(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSub(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSub(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineMul(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeMul(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineDiv(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeDiv(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineMod(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeMod(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineModNumber(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeModNumber(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineNegate(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeNegate(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineEqual(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeEqual(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineGreater(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeGreater(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineLess(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeLess(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineNot(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeNot(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" std::uint8_t minijsBaselinePeekTruthy(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return 2;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimePeekTruthy(*frame);
  } catch (const minijs::RuntimeError& error) {
    markFailedFromException(frame, error);
    return 2;
  } catch (const std::exception& error) {
    markFailedFromException(frame, error);
    return 2;
  } catch (...) {
    markFailed(frame);
    return 2;
  }
}

extern "C" bool minijsBaselineRecordLoopBackedge(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeRecordLoopBackedge(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineSyncStackSize(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeSyncStackSize(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineReturnLocal(minijs::BaselineFrame* frame, std::uint32_t slot) {
  if (!frameCanRun(frame)) return false;
  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeReturnLocal(*frame, slot);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineReturnNumberBits(minijs::BaselineFrame* frame, std::uint64_t bits) {
  if (!frameCanRun(frame)) return false;
  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeReturnNumberBits(*frame, bits);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineReturnConstant(minijs::BaselineFrame* frame, std::uint32_t constantIndex) {
  if (!frameCanRun(frame)) return false;
  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeReturnConstant(*frame, constantIndex);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}

extern "C" bool minijsBaselineReturn(minijs::BaselineFrame* frame) {
  if (!frameCanRun(frame)) {
    return false;
  }

  try {
    HelperProfileScope profile(*frame->vm, __func__);
    return frame->vm->baselineRuntimeReturn(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}
