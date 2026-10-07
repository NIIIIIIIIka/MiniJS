#pragma once

#include <cstdint>

#include "minijs/baseline_frame.h"
#include "minijs/value.h"

extern "C" bool minijsBaselinePush(minijs::BaselineFrame* frame, const minijs::Value* value);
extern "C" bool minijsBaselinePushConstant(minijs::BaselineFrame* frame,
                                            std::uint32_t constantIndex);
extern "C" bool minijsBaselineGetLocal(minijs::BaselineFrame* frame, std::uint32_t slot);
extern "C" bool minijsBaselineSetLocal(minijs::BaselineFrame* frame, std::uint32_t slot);
extern "C" bool minijsBaselinePop(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineDefineGlobal(minijs::BaselineFrame* frame,
                                            std::uint32_t nameIndex);
extern "C" bool minijsBaselineGetGlobal(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex);
extern "C" bool minijsBaselineSetGlobal(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex);
extern "C" bool minijsBaselineAdd(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineSub(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineMul(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineDiv(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineMod(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineModNumber(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineNegate(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineEqual(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineGreater(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineLess(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineNot(minijs::BaselineFrame* frame);
extern "C" std::uint8_t minijsBaselinePeekTruthy(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineRecordLoopBackedge(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineSyncStackSize(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineReturn(minijs::BaselineFrame* frame);
