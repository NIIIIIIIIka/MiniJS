#pragma once

namespace minijs {

// 未来由 VM 垃圾回收器统一管理的堆对象类型。
enum class ObjType {
  String,
  Array,
  Object,
  Function,
  Closure,
  Upvalue,
  Class,
  Instance,
  BoundMethod,
  NativeFunction,
};

// 所有 GC 堆对象共享的头部。next 用于挂入 VM 的对象链表。
struct Obj {
  explicit Obj(ObjType type);

  ObjType type;
  bool marked = false;
  Obj* next = nullptr;
};

}  // namespace minijs
