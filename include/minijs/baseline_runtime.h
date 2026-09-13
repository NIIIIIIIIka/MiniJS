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
extern "C" bool minijsBaselineAdd(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineSub(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineMul(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineDiv(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineMod(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineNegate(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineReturn(minijs::BaselineFrame* frame);
