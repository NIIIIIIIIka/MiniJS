#include "minijs/baseline_runtime.h"

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

}  // namespace

extern "C" bool minijsBaselinePush(minijs::BaselineFrame* frame, const minijs::Value* value) {
  if (!frameCanRun(frame) || value == nullptr) {
    return markFailed(frame);
  }

  try {
    return frame->vm->baselineRuntimePush(*frame, *value);
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
  } catch (...) {
    return markFailed(frame);
  }
}
