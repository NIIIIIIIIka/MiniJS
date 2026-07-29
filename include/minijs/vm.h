#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "minijs/bytecode_function.h"
#include "minijs/bytecode_closure.h"
#include "minijs/chunk.h"
#include "minijs/object.h"
#include "minijs/value.h"

namespace minijs {

struct ObjClosure;
struct ObjClass;
struct ObjUpvalue;
struct ObjFunction;

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

// 执行 Chunk 的栈式虚拟机。
class VM {
 public:
  VM();
  ~VM();

  Value run(const Chunk& chunk);
  std::size_t objectCount() const;

  void collectGarbage();

 private:
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
  Value makeGcString(std::string string);
  Value makeGcStringArray(std::vector<std::string> strings);
  ObjClosure* makeGcClosure(const BytecodeFunction& function);

  void push(Value value);
  Value pop();
  const Value& peek() const;
  void callBytecodeClosure(ObjClosure* closure, std::size_t argCount, std::size_t returnSlot,
                           std::size_t slotStart, const std::string& label,
                           bool returnsReceiver = false);
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
  // VM 构造期创建的 builtin 是永久全局根；测试计数只暴露用户程序产生的 GC 对象。
  std::size_t builtinObjectCount_ = 0;
  std::size_t nextGcObjectCount_ = 8;

  template <typename T, typename... Args>
  T* allocateObject(Args&&... args);
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
}  // namespace minijs
