#include "minijs/baseline_runtime.h"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "minijs/runtime_error.h"
#include "minijs/vm.h"

namespace {

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
    return frame->vm->baselineRuntimeGetLocal(*frame, slot);
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
    return frame->vm->baselineRuntimeSetLocal(*frame, slot);
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
    return frame->vm->baselineRuntimeSetIndex(*frame);
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
    return frame->vm->baselineRuntimeSyncStackSize(*frame);
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
    return frame->vm->baselineRuntimeReturn(*frame);
  } catch (const minijs::RuntimeError& error) {
    return markFailedFromException(frame, error);
  } catch (const std::exception& error) {
    return markFailedFromException(frame, error);
  } catch (...) {
    return markFailed(frame);
  }
}
