#include "minijs/vm.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

#include "minijs/gc_object.h"
#include "minijs/runtime_error.h"

namespace minijs {
namespace {

double checkedDivisor(const Value& value, const char* message) {
  const double divisor = value.asNumber();
  if (divisor == 0) {
    throw RuntimeError(message);
  }
  return divisor;
}

std::uint16_t readShort(const Chunk& chunk, std::size_t& ip) {
  const std::uint16_t high = chunk.readByte(ip++);
  const std::uint16_t low = chunk.readByte(ip++);
  return static_cast<std::uint16_t>((high << 8) | low);
}

std::size_t arrayIndexFromValue(const Value& value, std::size_t size) {
  const double index = value.asNumber();
  if (std::trunc(index) != index || index < 0) {
    throw RuntimeError("array index must be a non-negative integer");
  }

  const auto slot = static_cast<std::size_t>(index);
  if (slot >= size) {
    throw RuntimeError("array index out of bounds");
  }

  return slot;
}

void expectArity(std::size_t actual, std::size_t expected, const std::string& name) {
  if (actual != expected) {
    throw RuntimeError(name + " expects " + std::to_string(expected) + " argument" +
                       (expected == 1 ? "" : "s"));
  }
}

ObjClosure* findMethod(const ObjClass* klass, const std::string& name) {
  for (auto current = klass; current != nullptr; current = current->superclass) {
    const auto method = current->methods.find(name);
    if (method != current->methods.end()) {
      return method->second;
    }
  }
  return nullptr;
}

ObjClosure* findStaticMethod(const ObjClass* klass, const std::string& name) {
  for (auto current = klass; current != nullptr; current = current->superclass) {
    const auto method = current->staticMethods.find(name);
    if (method != current->staticMethods.end()) {
      return method->second;
    }
  }
  return nullptr;
}

bool isGcInstanceValue(const Value& value) {
  return value.isGcInstance();
}

ObjClass* gcInstanceClass(const Value& value) {
  return value.asGcInstance()->klass;
}

const std::unordered_map<std::string, Value>& gcInstanceFields(const Value& value) {
  return value.asGcInstance()->fields;
}

std::unordered_map<std::string, Value>& gcInstanceFields(Value& value) {
  return value.asGcInstance()->fields;
}

}  // namespace

VM::VM() {
  defineBuiltin("print", 1, [](const std::vector<Value>& arguments) -> Value {
    std::cout << arguments[0].toString() << '\n';
    return Value();
  });

  defineBuiltin("clock", 0, [](const std::vector<Value>&) -> Value { return Value(0.0); });

  defineBuiltin("len", 1, [](const std::vector<Value>& arguments) -> Value {
    const Value& value = arguments[0];

    if (value.isGcString()) {
      return Value(static_cast<double>(value.asGcString()->value.size()));
    }

    if (value.isGcArray()) {
      return Value(static_cast<double>(value.asGcArray()->elements.size()));
    }

    if (value.isGcObject()) {
      return Value(static_cast<double>(value.asGcObject()->properties.size()));
    }

    if (isGcInstanceValue(value)) {
      return Value(static_cast<double>(gcInstanceFields(value).size()));
    }

    throw RuntimeError("len expects string, array, object, or instance");
  });

  defineBuiltin("typeOf", 1, [this](const std::vector<Value>& arguments) -> Value {
    const Value& value = arguments[0];

    if (value.isNumber()) {
      return makeGcString("number");
    }
    if (value.isBoolean()) {
      return makeGcString("boolean");
    }
    if (value.isGcString()) {
      return makeGcString("string");
    }
    if (value.isGcArray()) {
      return makeGcString("array");
    }
    if (value.isGcObject()) {
      return makeGcString("object");
    }
    if (value.isGcClass()) {
      return makeGcString("class");
    }
    if (isGcInstanceValue(value)) {
      return makeGcString("instance");
    }
    if (value.isGcClosure() || value.isFunction()) {
      return makeGcString("function");
    }
    if (value.isNativeFunction()) {
      return makeGcString("builtin");
    }
    if (value.isNull()) {
      return makeGcString("null");
    }
    if (value.isUndefined()) {
      return makeGcString("undefined");
    }

    return makeGcString("unknown");
  });

  defineBuiltin("has", 2, [](const std::vector<Value>& arguments) -> Value {
    const Value& object = arguments[0];
    const Value& key = arguments[1];

    if (!key.isGcString()) {
      throw RuntimeError("has key must be a string");
    }

    const std::string& name = key.asGcString()->value;

    if (object.isGcObject()) {
      const auto& properties = object.asGcObject()->properties;
      return Value(properties.find(name) != properties.end());
    }
    if (isGcInstanceValue(object)) {
      const auto& fields = gcInstanceFields(object);
      return Value(fields.find(name) != fields.end());
    }
    if (object.isGcArray()) {
      return Value(name == "length" || name == "push" || name == "pop");
    }
    if (object.isGcString()) {
      return Value(name == "length");
    }

    return Value(false);
  });

  defineBuiltin("del", 2, [](const std::vector<Value>& arguments) -> Value {
    Value object = arguments[0];
    const Value& key = arguments[1];

    if (!key.isGcString()) {
      throw RuntimeError("del key must be a string");
    }

    const std::string& name = key.asGcString()->value;
    if (isGcInstanceValue(object)) {
      return Value(gcInstanceFields(object).erase(name) > 0);
    }
    if (object.isGcObject()) {
      return Value(object.asGcObject()->properties.erase(name) > 0);
    }

    return Value(false);
  });

  defineBuiltin("keys", 1, [this](const std::vector<Value>& arguments) -> Value {
    const Value& object = arguments[0];
    std::vector<std::string> keys;

    if (object.isGcObject()) {
      for (const auto& key : object.asGcObject()->properties) {
        keys.push_back(key.first);
      }
      return makeGcStringArray(std::move(keys));
    }
    if (isGcInstanceValue(object)) {
      for (const auto& field : gcInstanceFields(object)) {
        keys.push_back(field.first);
      }
      return makeGcStringArray(std::move(keys));
    }
    if (object.isGcArray()) {
      return makeGcStringArray({"length", "push", "pop"});
    }
    if (object.isGcString()) {
      return makeGcStringArray({"length"});
    }

    return makeGcStringArray({});
  });
}

VM::~VM() {
  Obj* object = objects_;
  while (object != nullptr) {
    Obj* next = object->next;
    delete object;
    object = next;
  }
  objectCount_ = 0;
}

void VM::defineBuiltin(std::string name, std::size_t arity, NativeFn function) {
  auto* native = allocateObject<ObjNativeFunction>(std::move(name), arity, std::move(function));
  ++permanentObjectCount_;

  globals_[native->name] = Value(native);
}

Value VM::makeGcString(std::string string) {
  return Value(allocateObject<ObjString>(std::move(string)));
}

Value VM::makeGcStringArray(std::vector<std::string> strings) {
  std::vector<Value> values;
  values.reserve(strings.size());
  const std::size_t rootStart = temporaryRoots_.size();

  for (std::string& string : strings) {
    auto* object = allocateObject<ObjString>(std::move(string));
    temporaryRoots_.push_back(object);
    values.push_back(Value(object));
  }

  auto* array = allocateObject<ObjArray>(std::move(values));
  temporaryRoots_.resize(rootStart);
  return Value(array);
}

ObjClosure* VM::makeGcClosure(const BytecodeFunction& function) {
  auto* gcFunction = allocateObject<ObjFunction>(function);
  temporaryRoots_.push_back(gcFunction);

  auto* closure = allocateObject<ObjClosure>(gcFunction);
  temporaryRoots_.pop_back();
  return closure;
}

Value VM::run(const Chunk& chunk) {
  stack_.clear();
  frames_.clear();
  openUpvalues_.clear();
  temporaryRoots_.clear();

  auto script = std::make_shared<BytecodeFunction>();
  script->name = "<script>";
  script->chunk = chunk;
  ObjFunction scriptFunction(*script);
  ObjClosure scriptClosure(&scriptFunction);

  frames_.push_back(CallFrame{&scriptClosure, 0, 0, 0});

  while (true) {
    CallFrame& frame = frames_.back();
    const Chunk& chunk = frame.closure->function->function.chunk;

    Opcode opcode = static_cast<Opcode>(chunk.readByte(frame.ip++));

    switch (opcode) {
      case Opcode::Constant: {
        const std::uint8_t index = chunk.readByte(frame.ip++);
        const Value& constant = chunk.constant(index);
        if (constant.isString()) {
          push(makeGcString(constant.asString()));
        } else {
          push(constant);
        }
        break;
      }
      case Opcode::Add: {
        Value right = pop();
        Value left = pop();
        if (left.isGcString() || right.isGcString()) {
          push(makeGcString(left.toString() + right.toString()));
        } else {
          push(Value(left.asNumber() + right.asNumber()));
        }
        break;
      }
      case Opcode::Sub: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() - right.asNumber()));
        break;
      }
      case Opcode::Mul: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() * right.asNumber()));
        break;
      }
      case Opcode::Div: {
        Value right = pop();
        Value left = pop();
        const double divisor = checkedDivisor(right, "division by zero");
        push(Value(left.asNumber() / divisor));
        break;
      }
      case Opcode::Mod: {
        Value right = pop();
        Value left = pop();
        const double divisor = checkedDivisor(right, "modulo by zero");
        push(Value(std::fmod(left.asNumber(), divisor)));
        break;
      }
      case Opcode::Negate: {
        push(Value(-pop().asNumber()));
        break;
      }
      case Opcode::Return: {
        Value result = pop();

        if (frames_.size() == 1) {
          return copyOutValue(result);
        }

        if (frame.returnsReceiver) {
          result = stack_[frame.slotStart];  // this
        }

        closeUpvalues(frame.slotStart);
        frames_.pop_back();
        stack_.resize(frame.returnSlot);
        push(result);

        break;
      }

      case Opcode::DefineGlobal: {
        const std::string name = chunk.constant(chunk.readByte(frame.ip++)).asString();
        globals_[name] = pop();
        break;
      }

      case Opcode::GetGlobal: {
        const std::string name = chunk.constant(chunk.readByte(frame.ip++)).asString();
        auto it = globals_.find(name);
        if (it == globals_.end()) {
          throw RuntimeError("undefined variable: " + name);
        }
        push(it->second);
        break;
      }
      case Opcode::SetGlobal: {
        const std::string name = chunk.constant(chunk.readByte(frame.ip++)).asString();
        auto it = globals_.find(name);
        if (it == globals_.end()) {
          throw RuntimeError("undefined variable: " + name);
        }
        it->second = peek();
        break;
      }
      case Opcode::GetLocal: {
        const std::uint8_t slot = chunk.readByte(frame.ip++);
        const std::size_t absoluteSlot = frame.slotStart + slot;
        if (absoluteSlot >= stack_.size()) {
          throw RuntimeError("local slot out of bounds");
        }
        push(stack_[absoluteSlot]);
        break;
      }
      case Opcode::SetLocal: {
        const std::uint8_t slot = chunk.readByte(frame.ip++);
        const std::size_t absoluteSlot = frame.slotStart + slot;
        if (absoluteSlot >= stack_.size()) {
          throw RuntimeError("local slot out of bounds");
        }
        stack_[absoluteSlot] = peek();
        break;
      }
      case Opcode::Pop:
        pop();
        break;
      case Opcode::Not: {
        push(Value(!pop().isTruthy()));
        break;
      }
      case Opcode::Equal: {
        Value right = pop();
        Value left = pop();
        push(Value(left.equals(right)));
        break;
      }
      case Opcode::Greater: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() > right.asNumber()));
        break;
      }
      case Opcode::Less: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() < right.asNumber()));
        break;
      }
      case Opcode::JumpIfFalse: {
        const std::uint16_t offset = readShort(chunk, frame.ip);
        if (!peek().isTruthy()) {
          frame.ip += offset;
        }
        break;
      }
      case Opcode::Jump: {
        const std::uint16_t offset = readShort(chunk, frame.ip);
        frame.ip += offset;
        break;
      }
      case Opcode::Loop: {
        const std::uint16_t offset = readShort(chunk, frame.ip);
        frame.ip -= offset;
        break;
      }
      case Opcode::Call: {
        const std::uint8_t argCount = chunk.readByte(frame.ip++);
        if (stack_.size() < static_cast<std::size_t>(argCount) + 1) {
          throw RuntimeError("call stack underflow");
        }

        const std::size_t calleeIndex = stack_.size() - argCount - 1;
        Value callee = stack_[calleeIndex];

        if (callee.isNativeFunction()) {
          const std::string& name = callee.isGcNativeFunction()
                                        ? callee.asGcNativeFunction()->name
                                        : callee.asNativeFunction()->name;
          const std::size_t arity = callee.isGcNativeFunction()
                                        ? callee.asGcNativeFunction()->arity
                                        : callee.asNativeFunction()->arity;
          expectArity(argCount, arity, name);
            
          std::vector<Value> arguments;
          arguments.reserve(argCount);
          for (std::size_t i = 0; i < argCount; ++i) {
            arguments.push_back(stack_[calleeIndex + 1 + i]);
          }

          Value result = callee.isGcNativeFunction()
                             ? callee.asGcNativeFunction()->function(arguments)
                             : callee.asNativeFunction()->function(arguments);
          stack_.resize(calleeIndex);
          push(result);
          break;
        }

        if (callee.isGcClass()) {
          auto klass = callee.asGcClass();
          auto init = findMethod(klass, "init");
          if (init == nullptr && argCount != 0) {
            throw RuntimeError("class " + klass->name + " expects 0 arguments");
          }

          auto* instance = allocateObject<ObjInstance>(klass);
          if (init == nullptr) {
            stack_.resize(calleeIndex);
            push(Value(instance));
            break;
          }

          stack_[calleeIndex] = Value(instance);
          callBytecodeClosure(init, argCount, calleeIndex, calleeIndex, "method", true);
          break;
        }

        if (callee.isGcBoundMethod()) {
          auto* boundMethod = callee.asGcBoundMethod();
          stack_[calleeIndex] = boundMethod->receiver;
          callBytecodeClosure(boundMethod->method, argCount, calleeIndex, calleeIndex, "method");
          break;
        }

        if (callee.isGcClosure()) {
          // callee 位于参数前一个槽位；新函数帧从第一个参数开始。
          // 因此 OP_GET_LOCAL 0 读取的就是第一个实参，无需复制参数数组。
          callBytecodeClosure(callee.asGcClosure(), argCount, calleeIndex, calleeIndex + 1,
                              "function");
          break;
        }

        throw RuntimeError("value is not callable");
      }
      case Opcode::Class: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        push(Value(allocateObject<ObjClass>(chunk.constant(nameIndex).asString())));
        break;
      }
      case Opcode::Method: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();
        Value method = pop();
        Value klass = peek();
        klass.asGcClass()->methods[name] = method.asGcClosure();
        break;
      }
      case Opcode::StaticMethod: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();
        Value method = pop();
        Value klass = peek();
        klass.asGcClass()->staticMethods[name] = method.asGcClosure();
        break;
      }
      case Opcode::Inherit: {
        Value superclass = pop();
        Value subclass = peek();
        if (!superclass.isGcClass()) {
          throw RuntimeError("superclass must be a class");
        }
        if (!subclass.isGcClass()) {
          throw RuntimeError("subclass must be a class");
        }

        subclass.asGcClass()->superclass = superclass.asGcClass();
        break;
      }
      case Opcode::Closure: {
        const std::uint8_t functionIndex = chunk.readByte(frame.ip++);
        auto function = chunk.constant(functionIndex).asBytecodeFunction();
        auto* closure = makeGcClosure(*function);
        push(Value(closure));

        const BytecodeFunction& gcFunction = closure->function->function;
        closure->upvalues.reserve(gcFunction.upvalues.size());

        for (const UpvalueDescriptor& descriptor : gcFunction.upvalues) {
          const bool isLocal = chunk.readByte(frame.ip++) != 0;
          const std::uint8_t index = chunk.readByte(frame.ip++);
          if (isLocal != descriptor.isLocal || index != descriptor.index) {
            throw RuntimeError("closure upvalue metadata mismatch");
          }

          if (isLocal) {
            closure->upvalues.push_back(captureUpvalue(frame.slotStart + index));
          } else {
            closure->upvalues.push_back(frame.closure->upvalues[index]);
          }
        }

        break;
      }
      case Opcode::GetUpvalue: {
        const std::uint8_t slot = chunk.readByte(frame.ip++);
        const auto& upvalue = frame.closure->upvalues[slot];
        push(upvalue->isClosed ? upvalue->closed : stack_[upvalue->stackIndex]);
        break;
      }
      case Opcode::SetUpvalue: {
        const std::uint8_t slot = chunk.readByte(frame.ip++);
        const auto& upvalue = frame.closure->upvalues[slot];
        if (upvalue->isClosed) {
          upvalue->closed = peek();
        } else {
          stack_[upvalue->stackIndex] = peek();
        }
        break;
      }
      case Opcode::CloseUpvalue: {
        closeUpvalues(stack_.size() - 1);
        pop();
        break;
      }
      case Opcode::MethodCall: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();
        const std::uint8_t argCount = chunk.readByte(frame.ip++);
        if (stack_.size() < static_cast<std::size_t>(argCount) + 1) {
          throw RuntimeError("method call stack underflow");
        }

        const std::size_t receiverIndex = stack_.size() - argCount - 1;
        Value receiver = stack_[receiverIndex];
        if (isGcInstanceValue(receiver)) {
          auto method = findMethod(gcInstanceClass(receiver), name);
          if (method == nullptr) {
            throw RuntimeError("value has no method: " + name);
          }

          callBytecodeClosure(method, argCount, receiverIndex, receiverIndex, "method");
          break;
        }

        if (receiver.isGcClass()) {
          auto method = findStaticMethod(receiver.asGcClass(), name);
          if (method == nullptr) {
            throw RuntimeError("value has no method: " + name);
          }

          callBytecodeClosure(method, argCount, receiverIndex, receiverIndex + 1, "function");
          break;
        }

        if (!receiver.isGcArray()) {
          throw RuntimeError("method call receiver is not an array");
        }

        std::vector<Value>& array = receiver.asGcArray()->elements;

        if (name == "push") {
          if (argCount != 1) {
            throw RuntimeError("push expects 1 argument");
          }

          array.push_back(stack_[receiverIndex + 1]);
          stack_.resize(receiverIndex);
          push(Value(static_cast<double>(array.size())));
          break;
        }

        if (name == "pop") {
          if (argCount != 0) {
            throw RuntimeError("pop expects 0 arguments");
          }

          if (array.empty()) {
            stack_.resize(receiverIndex);
            push(Value::undefined());
            break;
          }

          Value value = array.back();
          array.pop_back();
          stack_.resize(receiverIndex);
          push(value);
          break;
        }

        throw RuntimeError("unknown array method: " + name);
      }
      case Opcode::SuperCall: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();
        const std::uint8_t argCount = chunk.readByte(frame.ip++);
        if (stack_.size() < static_cast<std::size_t>(argCount) + 2) {
          throw RuntimeError("super call stack underflow");
        }

        const std::size_t receiverIndex = stack_.size() - argCount - 1;
        const std::size_t superclassIndex = receiverIndex - 1;
        Value superclass = stack_[superclassIndex];
        Value receiver = stack_[receiverIndex];
        if (!superclass.isGcClass()) {
          throw RuntimeError("superclass must be a class");
        }
        if (!isGcInstanceValue(receiver)) {
          throw RuntimeError("this must be an instance");
        }

        auto method = findMethod(superclass.asGcClass(), name);
        if (method == nullptr) {
          throw RuntimeError("superclass has no method: " + name);
        }

        // 重排前栈布局：[..., superclass, receiver, arg0, arg1, ...]
        // 重排后栈布局：[..., receiver, arg0, arg1, ...]
        // 方法从 superclass 上查找，但 local 0 / this 仍然是当前 receiver。
        stack_[superclassIndex] = receiver;
        for (std::size_t i = 0; i < argCount; ++i) {
          stack_[superclassIndex + 1 + i] = stack_[receiverIndex + 1 + i];
        }
        stack_.resize(superclassIndex + 1 + argCount);
        callBytecodeClosure(method, argCount, superclassIndex, superclassIndex, "method");
        break;
      }
      case Opcode::Array: {
        const std::uint8_t count = chunk.readByte(frame.ip++);
        collectGarbageIfNeeded();

        std::vector<Value> elements(count);
        for (std::size_t i = count; i > 0; --i) {
          elements[i - 1] = pop();
        }
        push(Value(allocateObject<ObjArray>(std::move(elements))));
        break;
      }
      case Opcode::GetIndex: {
        Value index = pop();
        Value array = pop();
        if (!array.isGcArray()) {
          throw RuntimeError("value is not an array");
        }
        const std::vector<Value>& elements = array.asGcArray()->elements;
        push(elements[arrayIndexFromValue(index, elements.size())]);
        break;
      }
      case Opcode::SetIndex: {
        Value value = pop();
        Value index = pop();
        Value array = pop();
        if (!array.isGcArray()) {
          throw RuntimeError("value is not an array");
        }
        std::vector<Value>& elements = array.asGcArray()->elements;
        elements[arrayIndexFromValue(index, elements.size())] = value;
        push(value);
        break;
      }
      case Opcode::Object: {
        const std::uint8_t namesIndex = chunk.readByte(frame.ip++);
        const std::vector<Value>& names = chunk.constant(namesIndex).asArray();

        collectGarbageIfNeeded();

        std::unordered_map<std::string, Value> properties;
        for (std::size_t i = names.size(); i > 0; --i) {
          Value value = pop();
          const std::string& name = names[i - 1].asString();
          properties[name] = value;
        }
        push(Value(allocateObject<ObjObject>(std::move(properties))));
        break;
      }
      case Opcode::GetProperty: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();

        Value object = pop();

        if (object.isGcArray()) {
          if (name == "length") {
            push(Value(static_cast<double>(object.asGcArray()->elements.size())));
            break;
          }

          push(Value::undefined());
          break;
        }

        if (object.isGcString()) {
          if (name == "length") {
            push(Value(static_cast<double>(object.asGcString()->value.size())));
            break;
          }

          push(Value::undefined());
          break;
        }

        if (isGcInstanceValue(object)) {
          auto& fields = gcInstanceFields(object);
          auto field = fields.find(name);
          if (field != fields.end()) {
            push(field->second);
            break;
          }

          auto method = findMethod(gcInstanceClass(object), name);
          if (method != nullptr) {
            push(object);
            auto* boundMethod = allocateObject<ObjBoundMethod>(object, method);
            pop();
            push(Value(boundMethod));
            break;
          }

          push(Value::undefined());
          break;
        }

        if (object.isGcClass()) {
          auto method = findStaticMethod(object.asGcClass(), name);
          if (method != nullptr) {
            push(Value(method));
            break;
          }

          push(Value::undefined());
          break;
        }

        if (!object.isGcObject()) {
          throw RuntimeError("value is not an object");
        }

        const auto& properties = object.asGcObject()->properties;

        auto it = properties.find(name);
        if (it == properties.end()) {
          push(Value::undefined());
        } else {
          push(it->second);
        }
        break;
      }
      case Opcode::SetProperty: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();

        Value value = pop();
        Value object = pop();
        if (isGcInstanceValue(object)) {
          gcInstanceFields(object)[name] = value;
          push(value);
          break;
        }

        if (!object.isGcObject()) {
          throw RuntimeError("value is not an object");
        }

        object.asGcObject()->properties[name] = value;
        push(value);

        break;
      }
      case Opcode::GetCurrentClosure:
        // 局部函数递归使用：把当前调用帧的 closure 压栈作为 callee。
        push(Value(frame.closure));
        break;
    }
  }
}

std::size_t VM::objectCount() const {
  return objectCount_ - permanentObjectCount_;
}

Value VM::copyOutValue(const Value& value) const {
  if (value.isGcString()) {
    return Value(value.asGcString()->value);
  }

  if (value.isGcArray()) {
    std::vector<Value> elements;
    elements.reserve(value.asGcArray()->elements.size());
    for (const Value& element : value.asGcArray()->elements) {
      elements.push_back(copyOutValue(element));
    }
    return Value(std::move(elements));
  }

  if (value.isGcObject()) {
    std::unordered_map<std::string, Value> properties;
    for (const auto& property : value.asGcObject()->properties) {
      properties[property.first] = copyOutValue(property.second);
    }
    return Value(std::move(properties));
  }

  if (value.isGcInstance()) {
    auto instance = std::make_shared<BytecodeInstance>();
    instance->klass = copyOutClass(value.asGcInstance()->klass);
    for (const auto& field : value.asGcInstance()->fields) {
      instance->fields[field.first] = copyOutValue(field.second);
    }
    return Value(instance);
  }

  if (value.isGcClass()) {
    return Value(copyOutClass(value.asGcClass()));
  }

  if (value.isGcClosure()) {
    return Value(copyOutClosure(value.asGcClosure()));
  }

  if (value.isGcBoundMethod()) {
    auto method = std::make_shared<BytecodeBoundMethod>();
    method->receiver = copyOutValue(value.asGcBoundMethod()->receiver);
    method->method = copyOutClosure(value.asGcBoundMethod()->method);
    return Value(method);
  }

  if (value.isGcNativeFunction()) {
    auto native = std::make_shared<NativeFunction>();
    native->name = value.asGcNativeFunction()->name;
    native->arity = value.asGcNativeFunction()->arity;
    native->function = value.asGcNativeFunction()->function;
    return Value(native);
  }

  if (value.isArray()) {
    std::vector<Value> elements;
    elements.reserve(value.asArray().size());
    for (const Value& element : value.asArray()) {
      elements.push_back(copyOutValue(element));
    }
    return Value(std::move(elements));
  }

  if (value.isObject()) {
    std::unordered_map<std::string, Value> properties;
    for (const auto& property : value.asObject()) {
      properties[property.first] = copyOutValue(property.second);
    }
    return Value(std::move(properties));
  }

  if (value.isString()) {
    return Value(value.asString());
  }

  return value;
}

std::shared_ptr<BytecodeClass> VM::copyOutClass(const ObjClass* klass) const {
  if (klass == nullptr) {
    return nullptr;
  }

  auto copy = std::make_shared<BytecodeClass>();
  copy->name = klass->name;
  copy->superclass = copyOutClass(klass->superclass);
  for (const auto& method : klass->methods) {
    copy->methods[method.first] = copyOutClosure(method.second);
  }
  for (const auto& method : klass->staticMethods) {
    copy->staticMethods[method.first] = copyOutClosure(method.second);
  }
  return copy;
}

std::shared_ptr<BytecodeClosure> VM::copyOutClosure(const ObjClosure* closure) const {
  if (closure == nullptr) {
    return nullptr;
  }

  auto copy = std::make_shared<BytecodeClosure>();
  copy->function = std::make_shared<BytecodeFunction>(closure->function->function);
  for (const auto* upvalue : closure->upvalues) {
    copy->upvalues.push_back(copyOutUpvalue(upvalue));
  }
  return copy;
}

std::shared_ptr<Upvalue> VM::copyOutUpvalue(const ObjUpvalue* upvalue) const {
  if (upvalue == nullptr) {
    return nullptr;
  }

  auto copy = std::make_shared<Upvalue>();
  copy->stackIndex = upvalue->stackIndex;
  copy->closed = copyOutValue(upvalue->closed);
  copy->isClosed = upvalue->isClosed;
  return copy;
}

void VM::push(Value value) { stack_.push_back(std::move(value)); }

Value VM::pop() {
  if (stack_.empty()) {
    throw RuntimeError("bytecode stack underflow");
  }

  Value value = stack_.back();
  stack_.pop_back();
  return value;
}

const Value& VM::peek() const {
  if (stack_.empty()) {
    throw RuntimeError("bytecode stack underflow");
  }
  return stack_.back();
}

void VM::callBytecodeClosure(ObjClosure* closure, std::size_t argCount, std::size_t returnSlot,
                             std::size_t slotStart, const std::string& label,
                             bool returnsReceiver) {
  const BytecodeFunction& function = closure->function->function;
  if (argCount != function.params.size()) {
    throw RuntimeError(label + " " + function.name + " expects " +
                       std::to_string(function.params.size()) + " arguments");
  }

  frames_.push_back(CallFrame{
      closure,
      0,
      returnSlot,
      slotStart,
      returnsReceiver,
  });
}

// 捕获仍在 VM 栈上的局部变量。
// 同一个栈槽只创建一个 open upvalue，兄弟闭包共享同一个 Upvalue。
ObjUpvalue* VM::captureUpvalue(std::size_t stackIndex) {
  for (auto* upvalue : openUpvalues_) {
    if (!upvalue->isClosed && upvalue->stackIndex == stackIndex) {
      return upvalue;
    }
  }

  auto* upvalue = allocateObject<ObjUpvalue>(stackIndex);
  openUpvalues_.push_back(upvalue);
  return upvalue;
}

// 关闭所有指向即将离开栈的 upvalue。
// 值从 stack_ 复制到 Upvalue::closed，之后闭包从 closed 读写。
void VM::closeUpvalues(std::size_t firstStackIndex) {
  for (auto* upvalue : openUpvalues_) {
    if (!upvalue->isClosed && upvalue->stackIndex >= firstStackIndex) {
      upvalue->closed = stack_[upvalue->stackIndex];
      upvalue->isClosed = true;
    }
  }
  openUpvalues_.erase(
      std::remove_if(openUpvalues_.begin(), openUpvalues_.end(),
                     [](ObjUpvalue* upvalue) { return upvalue->isClosed; }),
      openUpvalues_.end());
}

void VM::markRoots() {
  for (const Value& value : stack_) {
    markValue(value);
  }

  for (const auto& global : globals_) {
    markValue(global.second);
  }

  for (const auto& upvalue : openUpvalues_) {
    markUpvalue(upvalue);
  }

  for (const CallFrame& frame : frames_) {
    markClosure(frame.closure);
  }

  for (Obj* object : temporaryRoots_) {
    markObject(object);
  }
}

void VM::collectGarbageIfNeeded() {
  if (objectCount_ + 1 <= nextGcObjectCount_) {
    return;
  }

  collectGarbage();
  nextGcObjectCount_ = std::max<std::size_t>(objectCount_ * 2, 8);
}

void VM::markValue(const Value& value) {
  if (value.isGcString()) {
    markObject(value.asGcString());
    return;
  }

  if (value.isGcArray()) {
    markObject(value.asGcArray());
    return;
  }

  if (value.isGcObject()) {
    markObject(value.asGcObject());
    return;
  }

  if (value.isGcInstance()) {
    markObject(value.asGcInstance());
    return;
  }

  if (value.isGcBoundMethod()) {
    markObject(value.asGcBoundMethod());
    return;
  }

  if (value.isGcClass()) {
    markObject(value.asGcClass());
    return;
  }

  if (value.isGcClosure()) {
    markObject(value.asGcClosure());
    return;
  }

  if (value.isGcNativeFunction()) {
    markObject(value.asGcNativeFunction());
    return;
  }

  if (value.isArray()) {
    for (const Value& element : value.asArray()) {
      markValue(element);
    }
    return;
  }

  if (value.isObject()) {
    for (const auto& property : value.asObject()) {
      markValue(property.second);
    }
    return;
  }

  if (value.isBytecodeClosure()) {
    markBytecodeClosure(value.asBytecodeClosure());
    return;
  }

  if (value.isBytecodeClass()) {
    markBytecodeClass(value.asBytecodeClass());
    return;
  }

  if (value.isBytecodeInstance()) {
    markBytecodeInstance(value.asBytecodeInstance());
    return;
  }

  if (value.isBytecodeBoundMethod()) {
    markBytecodeBoundMethod(value.asBytecodeBoundMethod());
  }
}

void VM::markObject(Obj* object) {
  if (object == nullptr || object->marked) {
    return;
  }

  object->marked = true;
  markObjectChildren(object);
}

void VM::markObjectChildren(Obj* object) {
  switch (object->type) {
    case ObjType::String:
      break;
    case ObjType::Array: {
      auto* array = static_cast<ObjArray*>(object);
      for (const Value& element : array->elements) {
        markValue(element);
      }
      break;
    }
    case ObjType::Object: {
      auto* objectValue = static_cast<ObjObject*>(object);
      for (const auto& property : objectValue->properties) {
        markValue(property.second);
      }
      break;
    }
    case ObjType::Function: {
      auto* function = static_cast<ObjFunction*>(object);

      for (const Value& constant : function->function.chunk.constants()) {
        markValue(constant);
      }

      break;
    }
    case ObjType::Upvalue: {
      auto* upvalue = static_cast<ObjUpvalue*>(object);
      if (upvalue->isClosed) {
        markValue(upvalue->closed);
      } else if (upvalue->stackIndex < stack_.size()) {
        markValue(stack_[upvalue->stackIndex]);
      }
      break;
    }
    case ObjType::Closure: {
      auto* closure = static_cast<ObjClosure*>(object);
      markObject(closure->function);
      for (const auto& upvalue : closure->upvalues) {
        markUpvalue(upvalue);
      }
      break;
    }
    case ObjType::Class: {
      auto* klass = static_cast<ObjClass*>(object);
      markObject(klass->superclass);
      for (const auto& method : klass->methods) {
        markClosure(method.second);
      }
      for (const auto& method : klass->staticMethods) {
        markClosure(method.second);
      }
      break;
    }
    case ObjType::Instance: {
      auto* instance = static_cast<ObjInstance*>(object);
      markObject(instance->klass);
      for (const auto& field : instance->fields) {
        markValue(field.second);
      }
      break;
    }
    case ObjType::BoundMethod: {
      auto* method = static_cast<ObjBoundMethod*>(object);
      markValue(method->receiver);
      markClosure(method->method);
      break;
    }
    case ObjType::NativeFunction:
      break;
  }
}

void VM::markClosure(ObjClosure* closure) {
  if (closure == nullptr) {
    return;
  }

  markObject(closure);
}

void VM::markBytecodeClosure(const std::shared_ptr<BytecodeClosure>& closure) {
  if (closure == nullptr) {
    return;
  }

  for (const auto& upvalue : closure->upvalues) {
    if (upvalue == nullptr) {
      continue;
    }

    if (upvalue->isClosed) {
      markValue(upvalue->closed);
    } else if (upvalue->stackIndex < stack_.size()) {
      markValue(stack_[upvalue->stackIndex]);
    }
  }
}

void VM::markBytecodeClass(const std::shared_ptr<BytecodeClass>& klass) {
  if (klass == nullptr) {
    return;
  }

  markBytecodeClass(klass->superclass);

  for (const auto& method : klass->methods) {
    markBytecodeClosure(method.second);
  }

  for (const auto& method : klass->staticMethods) {
    markBytecodeClosure(method.second);
  }
}

void VM::markBytecodeInstance(const std::shared_ptr<BytecodeInstance>& instance) {
  if (instance == nullptr) {
    return;
  }

  markBytecodeClass(instance->klass);

  for (const auto& field : instance->fields) {
    markValue(field.second);
  }
}

void VM::markBytecodeBoundMethod(const std::shared_ptr<BytecodeBoundMethod>& method) {
  if (method == nullptr) {
    return;
  }

  markValue(method->receiver);
  markBytecodeClosure(method->method);
}

void VM::markUpvalue(ObjUpvalue* upvalue) {
  if (upvalue == nullptr) {
    return;
  }

  markObject(upvalue);
}

void VM::sweep() {
  Obj* previous = nullptr;
  Obj* object = objects_;

  while (object != nullptr) {
    if (object->marked) {
      object->marked = false;
      previous = object;
      object = object->next;
      continue;
    }

    Obj* unreached = object;
    object = object->next;

    if (previous == nullptr) {
      objects_ = object;
    } else {
      previous->next = object;
    }
    --objectCount_;
    delete unreached;
  }
}

void VM::collectGarbage() {
  markRoots();
  sweep();
}

}  // namespace minijs
