#pragma once

#include <cstdint>

namespace minijs::detail {

// 将平台 ABI 参数准备集中在同一处。具体寄存器及指令编码仍由 emitter 决定。
template <typename Emitter>
void emitRuntimeCall(Emitter& emitter, std::uintptr_t functionAddress) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitLoadHelper(functionAddress);
  emitter.emitCallHelper();
}

template <typename Emitter>
void emitRuntimeCallWithU32(Emitter& emitter, std::uintptr_t functionAddress,
                            std::uint32_t argument) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitMoveU32ToSecondArg(argument);
  emitter.emitLoadHelper(functionAddress);
  emitter.emitCallHelper();
}

template <typename Emitter>
void emitRuntimeCallWithTwoU32(Emitter& emitter, std::uintptr_t functionAddress,
                               std::uint32_t firstArgument,
                               std::uint32_t secondArgument) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitMoveU32ToSecondArg(firstArgument);
  emitter.emitMoveU32ToThirdArg(secondArgument);
  emitter.emitLoadHelper(functionAddress);
  emitter.emitCallHelper();
}

template <typename Emitter>
void emitRuntimeCallWithThreeU32(Emitter& emitter, std::uintptr_t functionAddress,
                                 std::uint32_t firstArgument,
                                 std::uint32_t secondArgument,
                                 std::uint32_t thirdArgument) {
  emitter.emitMoveFrameToFirstArg();
  emitter.emitMoveU32ToSecondArg(firstArgument);
  emitter.emitMoveU32ToThirdArg(secondArgument);
  emitter.emitMoveU32ToFourthArg(thirdArgument);
  emitter.emitLoadHelper(functionAddress);
  emitter.emitCallHelper();
}

}  // namespace minijs::detail
