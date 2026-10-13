#pragma once

#include <cstdint>

#include "minijs/baseline_frame.h"
#include "minijs/value.h"

// native emitter 将 helper 返回值按低字节零扩展；新增接口必须保持 bool/uint8_t 返回协议。
// 若改为其它返回类型，必须同步修改调用方的寄存器处理与对应 ABI 测试。
extern "C" bool minijsBaselinePush(minijs::BaselineFrame* frame, const minijs::Value* value);
extern "C" bool minijsBaselinePushConstant(minijs::BaselineFrame* frame,
                                            std::uint32_t constantIndex);
extern "C" bool minijsBaselineGetLocal(minijs::BaselineFrame* frame, std::uint32_t slot);
// 构造数字压栈槽并刷新栈快照，供 native GetLocal 写入载荷。
extern "C" bool minijsBaselinePushNumberSlot(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineGetLocalAndPushNumberSlot(minijs::BaselineFrame* frame,
                                                       std::uint32_t slot);
extern "C" bool minijsBaselineSetLocal(minijs::BaselineFrame* frame, std::uint32_t slot);
// 合并数字/布尔常量赋值语句，目标类型变化时仍更新完整 Value。
extern "C" bool minijsBaselineAssignConstant(minijs::BaselineFrame* frame,
                                            std::uint32_t constantIndex, std::uint32_t targetSlot);
// 合并结果被丢弃的局部赋值；非数字仍复制完整 Value。
extern "C" bool minijsBaselineCopyLocal(minijs::BaselineFrame* frame, std::uint32_t sourceSlot,
                                       std::uint32_t targetSlot);
extern "C" bool minijsBaselinePop(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineDefineGlobal(minijs::BaselineFrame* frame,
                                            std::uint32_t nameIndex);
extern "C" bool minijsBaselineGetGlobal(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex);
extern "C" bool minijsBaselineSetGlobal(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex);
extern "C" bool minijsBaselineArray(minijs::BaselineFrame* frame, std::uint32_t count);
extern "C" bool minijsBaselineGetIndex(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineSetIndex(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineObject(minijs::BaselineFrame* frame, std::uint32_t namesIndex);
extern "C" bool minijsBaselineGetProperty(minijs::BaselineFrame* frame,
                                           std::uint32_t nameIndex,
                                           std::uint32_t feedbackSlotIndex);
extern "C" bool minijsBaselineSetProperty(minijs::BaselineFrame* frame,
                                           std::uint32_t nameIndex,
                                           std::uint32_t feedbackSlotIndex);
extern "C" bool minijsBaselineClass(minijs::BaselineFrame* frame, std::uint32_t nameIndex);
extern "C" bool minijsBaselineMethod(minijs::BaselineFrame* frame, std::uint32_t nameIndex);
extern "C" bool minijsBaselineStaticMethod(minijs::BaselineFrame* frame,
                                            std::uint32_t nameIndex);
extern "C" bool minijsBaselineInherit(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineGetUpvalue(minijs::BaselineFrame* frame, std::uint32_t slot);
extern "C" bool minijsBaselineSetUpvalue(minijs::BaselineFrame* frame, std::uint32_t slot);
extern "C" bool minijsBaselineCloseUpvalue(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineGetCurrentClosure(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineClosure(minijs::BaselineFrame* frame,
                                       std::uint32_t functionIndex,
                                       std::uint32_t bytecodeOffset);
extern "C" bool minijsBaselineCall(minijs::BaselineFrame* frame, std::uint32_t argCount);
extern "C" bool minijsBaselineMethodCall(minijs::BaselineFrame* frame,
                                          std::uint32_t nameIndex,
                                          std::uint32_t argCount,
                                          std::uint32_t feedbackSlotIndex);
extern "C" bool minijsBaselineSuperCall(minijs::BaselineFrame* frame,
                                         std::uint32_t nameIndex,
                                         std::uint32_t argCount);
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
// 不复制或弹出局部值；0=falsey，1=truthy，2=失败。
extern "C" std::uint8_t minijsBaselineLocalTruthy(minijs::BaselineFrame* frame, std::uint32_t slot);
// 比较两个局部值并清理临时结果；0=false，1=true，2=失败。
extern "C" std::uint8_t minijsBaselineCompareLocalsTruthy(minijs::BaselineFrame* frame,
    std::uint32_t leftSlot, std::uint32_t rightSlot, std::uint32_t operation);
extern "C" std::uint8_t minijsBaselineCompareLocalConstantTruthy(minijs::BaselineFrame* frame,
    std::uint32_t leftSlot, std::uint32_t constantIndex, std::uint32_t operation);
extern "C" bool minijsBaselineRecordLoopBackedge(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineSyncStackSize(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineReturn(minijs::BaselineFrame* frame);
extern "C" bool minijsBaselineReturnLocal(minijs::BaselineFrame* frame, std::uint32_t slot);
extern "C" bool minijsBaselineReturnNumberBits(minijs::BaselineFrame* frame, std::uint64_t bits);
extern "C" bool minijsBaselineReturnConstant(minijs::BaselineFrame* frame, std::uint32_t constantIndex);
