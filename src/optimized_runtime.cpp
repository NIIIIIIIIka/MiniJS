#include "minijs/optimized_frame.h"

#include <exception>
#include <vector>

#include "minijs/optimized_code.h"
#include "minijs/gc_object.h"

extern "C" bool minijsExecuteOptimizedLowered(minijs::OptimizedFrame* frame) noexcept {
  if (frame == nullptr) return false;
  try {
    if (frame->code == nullptr ||
        (frame->argumentCount != 0 && frame->arguments == nullptr)) {
      frame->failed = true;
      frame->errorMessage = "invalid optimized frame";
      return false;
    }
    if (frame->safepointHook != nullptr) {
      frame->safepointHook(frame->safepointHookContext);
    }
    std::vector<minijs::Value> arguments;
    if (frame->argumentCount != 0) {
      arguments.assign(frame->arguments, frame->arguments + frame->argumentCount);
    }
    const minijs::OptimizedMethodCall& methodCall = frame->code->methodCall;
    if (methodCall.valid()) {
      if (methodCall.receiverArgument >= arguments.size() ||
          !arguments[methodCall.receiverArgument].isGcInstance() ||
          arguments[methodCall.receiverArgument].asGcInstance()->klass != methodCall.klass) {
        frame->guardFailed = true;
        frame->deoptId = methodCall.deoptId;
        frame->errorMessage = "optimized method receiver class guard failed";
        return false;
      }
      for (const std::uint32_t argumentIndex : methodCall.argumentIndices) {
        if (argumentIndex >= arguments.size()) {
          frame->failed = true;
          frame->errorMessage = "optimized method argument is unavailable";
          return false;
        }
      }
      frame->directCall = true;
      frame->directCallTarget = methodCall.method;
      return false;
    }
    const minijs::OptimizedPropertyAccess& property = frame->code->propertyAccess;
    if (property.kind != minijs::OptimizedPropertyAccessKind::None) {
      if (property.objectArgument >= arguments.size() ||
          !arguments[property.objectArgument].isGcObject()) {
        frame->guardFailed = true;
        frame->deoptId = property.deoptId;
        frame->errorMessage = "optimized property receiver guard failed";
        return false;
      }
      minijs::ObjObject* object = arguments[property.objectArgument].asGcObject();
      if (object->dictionaryMode || object->shape != property.shape ||
          property.slot >= object->slots.size()) {
        frame->guardFailed = true;
        frame->deoptId = property.deoptId;
        frame->errorMessage = "optimized property shape guard failed";
        return false;
      }
      if (property.kind == minijs::OptimizedPropertyAccessKind::Load) {
        frame->result = object->slots[property.slot];
      } else {
        if (property.valueArgument >= arguments.size()) {
          frame->failed = true;
          frame->errorMessage = "optimized property value argument is unavailable";
          return false;
        }
        object->slots[property.slot] = arguments[property.valueArgument];
        frame->result = arguments[property.valueArgument];
      }
      frame->completed = true;
      return true;
    }
    const minijs::OptimizerLoweredExecutionResult executed =
        minijs::executeOptimizerLoweredIr(frame->code->lowered, arguments);
    frame->completed = executed.completed;
    frame->guardFailed = executed.guardFailed;
    frame->deoptId = executed.deoptId;
    frame->result = executed.value;
    frame->errorMessage = executed.error;
    frame->failed = !executed.completed && !executed.guardFailed;
    return frame->completed;
  } catch (const std::exception& error) {
    frame->failed = true;
    frame->errorMessage = error.what();
  } catch (...) {
    frame->failed = true;
    frame->errorMessage = "unknown optimized runtime error";
  }
  return false;
}
