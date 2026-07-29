#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "minijs/bytecode_function.h"
#include "minijs/object.h"
#include "minijs/value.h"

namespace minijs {

struct ObjClass;
struct ObjClosure;
struct ObjFunction;
struct ObjNativeFunction;
struct ObjUpvalue;
struct Upvalue;

// VM GC 管理的数组对象，元素继续保存为动态 Value。
struct ObjArray final : public Obj {
  explicit ObjArray(std::vector<Value> elements);

  std::vector<Value> elements;
};

// VM GC 管理的对象字面量，属性值继续保存为动态 Value。
struct ObjObject final : public Obj {
  explicit ObjObject(std::unordered_map<std::string, Value> properties);

  std::unordered_map<std::string, Value> properties;
};

// VM GC 管理的字节码实例，字段保存在实例自身。
struct ObjInstance final : public Obj {
  explicit ObjInstance(ObjClass* klass);

  ObjClass* klass = nullptr;
  std::unordered_map<std::string, Value> fields;
};

// VM GC 管理的字节码绑定方法，保存调用接收者和方法闭包。
struct ObjBoundMethod final : public Obj {
  ObjBoundMethod(Value receiver, ObjClosure* method);

  Value receiver;
  ObjClosure* method = nullptr;
};

// VM GC 管理的字节码闭包，保存函数模板和捕获到的 upvalue。
struct ObjClosure final : public Obj {
  explicit ObjClosure(ObjFunction* function);

  ObjFunction* function = nullptr;
  std::vector<ObjUpvalue*> upvalues;
};

// VM GC 管理的字节码类对象，保存类名、父类和方法表。
struct ObjClass final : public Obj {
  explicit ObjClass(std::string name);

  std::string name;
  ObjClass* superclass = nullptr;
  std::unordered_map<std::string, ObjClosure*> methods;
  std::unordered_map<std::string, ObjClosure*> staticMethods;
};

struct ObjUpvalue final : public Obj {
  explicit ObjUpvalue(std::size_t stackIndex);

  std::size_t stackIndex = 0;
  Value closed = Value::undefined();
  bool isClosed = false;
};

// VM GC 管理的字节码函数模板；闭包持有它，调用帧从中读取 chunk、参数和 upvalue 描述。
struct ObjFunction final : public Obj {
  ObjFunction() = default;
  ObjFunction(BytecodeFunction function);
  BytecodeFunction function;
};

// VM GC 管理的原生函数；用于 print、clock、len 等内置函数。
struct ObjNativeFunction final : public Obj {
  ObjNativeFunction(std::string name, std::size_t arity, NativeFn function);

  std::string name;
  std::size_t arity = 0;
  NativeFn function;
};
}  // namespace minijs
