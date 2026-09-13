#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "minijs/baseline_frame.h"
#include "minijs/bytecode_function.h"
#include "minijs/bytecode_closure.h"
#include "minijs/chunk.h"
#include "minijs/object.h"
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

namespace minijs {

struct ObjClosure;
struct ObjClass;
struct ObjShape;
struct ObjUpvalue;
struct ObjFunction;
struct BaselineCode;

// 一次字节码函数调用的执行状态。
struct CallFrame {
  ObjClosure* closure = nullptr;
  std::size_t ip;
  // 函数返回值要写回的栈槽；普通调用是 callee 槽，方法调用是 receiver/this 槽。
  std::size_t returnSlot;
  // 当前函数局部变量区在 VM 栈中的起点。
  // 调用字节码函数时它指向第一个实参，因此参数 slot 0/1/... 可直接复用栈上的实参。
  // 调用字节码方法时它指向 receiver，因此 local 0 就是 this。
  std::size_t slotStart;
  // 构造器 init 专用：忽略函数体返回值，改为返回 local 0 的 receiver。
  bool returnsReceiver = false;
};

struct PropertyInlineCacheStats {
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t updates = 0;
  std::size_t bypasses = 0;

  std::size_t setHits = 0;
  std::size_t setMisses = 0;
  std::size_t setUpdates = 0;
  std::size_t setBypasses = 0;

  std::size_t methodCallInstanceDispatches = 0;
  std::size_t methodCallStaticDispatches = 0;
  std::size_t methodCallArrayPushes = 0;
  std::size_t methodCallArrayPops = 0;
  std::size_t methodCallDispatchErrors = 0;
  std::size_t methodCallHits = 0;
  std::size_t methodCallMisses = 0;
  std::size_t methodCallUpdates = 0;
  std::size_t methodCallBypasses = 0;
};

struct JitOptions {
  bool enabled = false;
  std::uint32_t callThreshold = 100;
  std::uint32_t backedgeThreshold = 1000;
};

// 执行 Chunk 的栈式虚拟机。
class VM {
 public:
  VM();
  ~VM();

  Value run(const Chunk& chunk);
  std::size_t objectCount() const;
  PropertyInlineCacheStats propertyInlineCacheStats() const;
  void resetPropertyInlineCacheStats();
  bool inlineCachesEnabled() const;
  void setInlineCachesEnabled(bool enabled);
  bool jitEnabled() const;
  void setJitEnabled(bool enabled);
  void setJitCallThreshold(std::uint32_t threshold);
  void setJitBackedgeThreshold(std::uint32_t threshold);

  void collectGarbage();

#ifdef MINIJS_TESTING
  bool debugHasRootObjectShape() const { return rootObjectShape_ != nullptr; }
  const ObjShape* debugGlobalObjectShape(const std::string& name) const;
  bool debugGlobalObjectUsesDictionary(const std::string& name) const;
  const JitFeedback* debugGlobalFunctionJitFeedback(const std::string& name) const;
  const JitFeedback* debugGlobalClassMethodJitFeedback(const std::string& className,
                                                       const std::string& methodName,
                                                       bool isStatic = false) const;
  const BaselineCode* debugGlobalFunctionBaselineCode(const std::string& name) const;
  std::string debugGlobalFunctionJitCompileError(const std::string& name) const;
  Value debugExecuteGlobalFunctionBaseline(const std::string& name,
                                           const std::vector<Value>& arguments);
  Value debugExecuteGlobalFunctionBaselineEntry(const std::string& name,
                                                const std::vector<Value>& arguments,
                                                BaselineEntry entry);
  PropertyInlineCacheStats debugPropertyInlineCacheStats() const;
  void debugResetPropertyInlineCacheStats();
#endif

 private:
  friend bool ::minijsBaselinePush(BaselineFrame* frame, const Value* value);
  friend bool ::minijsBaselinePushConstant(BaselineFrame* frame, std::uint32_t constantIndex);
  friend bool ::minijsBaselineGetLocal(BaselineFrame* frame, std::uint32_t slot);
  friend bool ::minijsBaselineSetLocal(BaselineFrame* frame, std::uint32_t slot);
  friend bool ::minijsBaselinePop(BaselineFrame* frame);
  friend bool ::minijsBaselineAdd(BaselineFrame* frame);
  friend bool ::minijsBaselineSub(BaselineFrame* frame);
  friend bool ::minijsBaselineMul(BaselineFrame* frame);
  friend bool ::minijsBaselineDiv(BaselineFrame* frame);
  friend bool ::minijsBaselineMod(BaselineFrame* frame);
  friend bool ::minijsBaselineNegate(BaselineFrame* frame);
  friend bool ::minijsBaselineReturn(BaselineFrame* frame);

  class TemporaryRootScope {
   public:
    explicit TemporaryRootScope(VM& vm);
    ~TemporaryRootScope();

    void add(Obj* object);

   private:
    VM& vm_;
    std::size_t rootStart_ = 0;
  };

  void defineBuiltin(std::string name, std::size_t arity, NativeFn function);
  // 对外兼容层：VM 内部已经使用 Obj* 运行时对象，这组函数只在 run() 返回前
  // 把 GC 对象转换成旧测试和外部接口仍能识别的 Bytecode*/Array/Object/String 值。
  // 后续删除 Bytecode* 兼容结构时，应优先收窄并移除这组 copyOut* 函数。
  Value copyOutValue(const Value& value) const;
  std::shared_ptr<BytecodeClass> copyOutClass(const ObjClass* klass) const;
  std::shared_ptr<BytecodeClosure> copyOutClosure(const ObjClosure* closure) const;
  std::shared_ptr<Upvalue> copyOutUpvalue(const ObjUpvalue* upvalue) const;

  // 对象属性：使用快访问
  std::size_t objectPropertyCount(const ObjObject& object) const;
  bool objectHasProperty(const ObjObject& object, const std::string& name) const;
  Value objectGetProperty(const ObjObject& object, const std::string& name) const;
  void objectSetProperty(ObjObject& object, const std::string& name, Value value);
  bool objectDeleteProperty(ObjObject& object, const std::string& name);
  std::vector<std::string> objectKeys(const ObjObject& object) const;
  ObjShape* transitionObjectShape(ObjShape* shape, const std::string& name);
  void objectMaterializeDictionary(ObjObject& object) const;

  Value makeGcString(std::string string);
  Value makeGcStringArray(std::vector<std::string> strings);
  ObjClosure* makeGcClosure(const BytecodeFunction& function);

  void push(Value value);
  Value pop();
  const Value& peek() const;
  void validateBaselineRuntimeFrame(const BaselineFrame& frame) const;
  bool baselineRuntimePush(BaselineFrame& frame, const Value& value);
  bool baselineRuntimePushConstant(BaselineFrame& frame, std::uint32_t constantIndex);
  bool baselineRuntimeGetLocal(BaselineFrame& frame, std::uint32_t slot);
  bool baselineRuntimeSetLocal(BaselineFrame& frame, std::uint32_t slot);
  bool baselineRuntimePop(BaselineFrame& frame);
  bool baselineRuntimeAdd(BaselineFrame& frame);
  bool baselineRuntimeSub(BaselineFrame& frame);
  bool baselineRuntimeMul(BaselineFrame& frame);
  bool baselineRuntimeDiv(BaselineFrame& frame);
  bool baselineRuntimeMod(BaselineFrame& frame);
  bool baselineRuntimeNegate(BaselineFrame& frame);
  bool baselineRuntimeReturn(BaselineFrame& frame);
  void executeDefineGlobal(const Chunk& chunk, std::size_t nameIndex);
  void executeGetGlobal(const Chunk& chunk, std::size_t nameIndex);
  void executeSetGlobal(const Chunk& chunk, std::size_t nameIndex);
  void executeArrayLiteral(std::size_t count);
  void executeGetIndex();
  void executeSetIndex();
  void executeObjectLiteral(const Chunk& chunk, std::size_t namesIndex);
  void executeGetProperty(const Chunk& chunk, std::size_t nameIndex,
                          std::size_t feedbackSlotIndex);
  void executeSetProperty(const Chunk& chunk, std::size_t nameIndex,
                          std::size_t feedbackSlotIndex);
  void executeGetUpvalue(const CallFrame& frame, std::size_t slot);
  void executeSetUpvalue(const CallFrame& frame, std::size_t slot);
  void executeCloseUpvalue();
  void executeGetCurrentClosure(const CallFrame& frame);
  void callBytecodeClosure(ObjClosure* closure, std::size_t argCount, std::size_t returnSlot,
                           std::size_t slotStart, const std::string& label,
                           bool returnsReceiver = false);
  void recordFunctionCall(ObjFunction& function);
  void recordLoopBackedge(ObjFunction& function);
  void maybeScheduleJit(BytecodeFunction& function);
  void compileScheduledJit(BytecodeFunction& function);
  Value executeBaselineCode(const BaselineCode& code, CallFrame& frame);
  ObjUpvalue* captureUpvalue(std::size_t stackIndex);
  void closeUpvalues(std::size_t firstStackIndex);
  void collectGarbageIfNeeded();
  void markRoots();
  void markValue(const Value& value);
  void markObject(Obj* object);
  void markObjectChildren(Obj* object);
  void markClosure(ObjClosure* closure);
  void markBytecodeClosure(const std::shared_ptr<BytecodeClosure>& closure);
  void markBytecodeClass(const std::shared_ptr<BytecodeClass>& klass);
  void markBytecodeInstance(const std::shared_ptr<BytecodeInstance>& instance);
  void markBytecodeBoundMethod(const std::shared_ptr<BytecodeBoundMethod>& method);
  void markUpvalue(ObjUpvalue* upvalue);
  void sweep();

  // 操作数栈，同时承载当前调用帧的参数和局部变量槽位。
  std::vector<Value> stack_;
  std::vector<CallFrame> frames_;
  std::vector<ObjUpvalue*> openUpvalues_;
  std::unordered_map<std::string, Value> globals_;
  // 保护尚未放入 stack_/globals_/frame 的中间 GC 对象，避免连续分配时被提前回收。
  std::vector<Obj*> temporaryRoots_;
  // 未来 GC 管理的堆对象链表。当前阶段只建立链表所有权入口。
  Obj* objects_ = nullptr;
  std::size_t heapObjectCount_ = 0;
  // VM 内部创建的永久对象不计入用户程序可见的 objectCount()。
  std::size_t internalObjectCount_ = 0;
  ObjShape* rootObjectShape_ = nullptr;
  std::size_t nextGcObjectCount_ = 8;
  PropertyInlineCacheStats propertyInlineCacheStats_;
  bool inlineCachesEnabled_ = true;
  JitOptions jitOptions_;

  template <typename T, typename... Args>
  T* allocateObject(Args&&... args);

  template <typename T, typename... Args>
  T* allocateInternalObject(Args&&... args);
};

template <typename T, typename... Args>
T* VM::allocateObject(Args&&... args) {
  static_assert(std::is_base_of_v<Obj, T>, "allocateObject requires an Obj-derived type");
  collectGarbageIfNeeded();

  auto* object = new T(std::forward<Args>(args)...);
  object->next = objects_;
  objects_ = object;
  ++heapObjectCount_;
  return object;
}

template <typename T, typename... Args>
T* VM::allocateInternalObject(Args&&... args) {
  T* object = allocateObject<T>(std::forward<Args>(args)...);
  ++internalObjectCount_;
  return object;
}
}  // namespace minijs
