#include "minijs/vm.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <utility>

#include "minijs/baseline_jit.h"
#include "minijs/gc_object.h"
#include "minijs/runtime_error.h"

namespace minijs {
namespace {

void incrementSaturating(std::uint32_t& value) {
  if (value != std::numeric_limits<std::uint32_t>::max()) {
    ++value;
  }
}

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

const PropertyInlineCacheEntry* findPropertyInlineCacheEntry(const PropertyInlineCache& cache,
                                                             const ObjObject& object) {
  for (std::size_t index = 0; index < cache.size; ++index) {
    const PropertyInlineCacheEntry& entry = cache.entries[index];
    if (entry.shape == object.shape && entry.slot < object.slots.size()) {
      return &entry;
    }
  }
  return nullptr;
}

bool updatePropertyFeedback(FeedbackSlot& feedback, ObjShape* shape, std::size_t slot) {
  PropertyInlineCache& cache = feedback.property;
  if (feedback.state == FeedbackState::Megamorphic) {
    return false;
  }
  if (cache.size == PropertyInlineCache::MaxEntries) {
    feedback.state = FeedbackState::Megamorphic;
    return false;
  }

  cache.entries[cache.size++] = PropertyInlineCacheEntry{shape, slot};
  feedback.state = cache.size == 1 ? FeedbackState::Monomorphic : FeedbackState::Polymorphic;
  return true;
}

const MethodInlineCacheEntry* findMethodInlineCacheEntry(const MethodInlineCache& cache,
                                                         const ObjClass* klass,
                                                         bool isStatic) {
  for (std::size_t index = 0; index < cache.size; ++index) {
    const MethodInlineCacheEntry& entry = cache.entries[index];
    if (entry.isStatic == isStatic && entry.klass == klass && entry.method != nullptr) {
      return &entry;
    }
  }
  return nullptr;
}

bool updateMethodFeedback(FeedbackSlot& feedback, ObjClass* klass, ObjClosure* method,
                          bool isStatic) {
  MethodInlineCache& cache = feedback.method;
  if (feedback.state == FeedbackState::Megamorphic) {
    return false;
  }
  if (cache.size == MethodInlineCache::MaxEntries) {
    feedback.state = FeedbackState::Megamorphic;
    return false;
  }

  cache.entries[cache.size++] = MethodInlineCacheEntry{klass, method, isStatic};
  feedback.state = cache.size == 1 ? FeedbackState::Monomorphic : FeedbackState::Polymorphic;
  return true;
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
  rootObjectShape_ = allocateInternalObject<ObjShape>(nullptr, "");

  defineBuiltin("print", 1, [](const std::vector<Value>& arguments) -> Value {
    std::cout << arguments[0].toString() << '\n';
    return Value();
  });

  defineBuiltin("clock", 0, [](const std::vector<Value>&) -> Value { return Value(0.0); });

  defineBuiltin("len", 1, [this](const std::vector<Value>& arguments) -> Value {
    const Value& value = arguments[0];

    if (value.isGcString()) {
      return Value(static_cast<double>(value.asGcString()->value.size()));
    }

    if (value.isGcArray()) {
      return Value(static_cast<double>(value.asGcArray()->elements.size()));
    }

    if (value.isGcObject()) {
      return Value(static_cast<double>(objectPropertyCount(*value.asGcObject())));
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

  defineBuiltin("has", 2, [this](const std::vector<Value>& arguments) -> Value {
    const Value& object = arguments[0];
    const Value& key = arguments[1];

    if (!key.isGcString()) {
      throw RuntimeError("has key must be a string");
    }

    const std::string& name = key.asGcString()->value;

    if (object.isGcObject()) {
      return Value(objectHasProperty(*object.asGcObject(), name));
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

  defineBuiltin("del", 2, [this](const std::vector<Value>& arguments) -> Value {
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
      return Value(objectDeleteProperty(*object.asGcObject(), name));
    }

    return Value(false);
  });

  defineBuiltin("keys", 1, [this](const std::vector<Value>& arguments) -> Value {
    const Value& object = arguments[0];
    std::vector<std::string> keys;

    if (object.isGcObject()) {
      return makeGcStringArray(objectKeys(*object.asGcObject()));
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
  heapObjectCount_ = 0;
}

VM::TemporaryRootScope::TemporaryRootScope(VM& vm)
    : vm_(vm), rootStart_(vm.temporaryRoots_.size()) {}

VM::TemporaryRootScope::~TemporaryRootScope() {
  vm_.temporaryRoots_.resize(rootStart_);
}

void VM::TemporaryRootScope::add(Obj* object) {
  vm_.temporaryRoots_.push_back(object);
}

void VM::defineBuiltin(std::string name, std::size_t arity, NativeFn function) {
  auto* native =
      allocateInternalObject<ObjNativeFunction>(std::move(name), arity, std::move(function));
  globals_[native->name] = Value(native);
}

Value VM::makeGcString(std::string string) {
  return Value(allocateObject<ObjString>(std::move(string)));
}

Value VM::makeGcStringArray(std::vector<std::string> strings) {
  std::vector<Value> values;
  values.reserve(strings.size());
  TemporaryRootScope roots(*this);

  for (std::string& string : strings) {
    auto* object = allocateObject<ObjString>(std::move(string));
    roots.add(object);
    values.push_back(Value(object));
  }

  auto* array = allocateObject<ObjArray>(std::move(values));
  return Value(array);
}

ObjClosure* VM::makeGcClosure(const BytecodeFunction& function) {
  TemporaryRootScope roots(*this);
  auto* gcFunction = allocateObject<ObjFunction>(function);
  roots.add(gcFunction);

  return allocateObject<ObjClosure>(gcFunction);
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
          Value copied = copyOutValue(result);
          frames_.clear();
          return copied;
        }

        const std::size_t slotStart = frame.slotStart;
        const std::size_t returnSlot = frame.returnSlot;
        const bool returnsReceiver = frame.returnsReceiver;

        if (returnsReceiver) {
          result = stack_[slotStart];  // this
        }

        closeUpvalues(slotStart);
        frames_.pop_back();
        stack_.resize(returnSlot);
        push(result);

        break;
      }

      case Opcode::DefineGlobal: {
        executeDefineGlobal(chunk, chunk.readByte(frame.ip++));
        break;
      }

      case Opcode::GetGlobal: {
        executeGetGlobal(chunk, chunk.readByte(frame.ip++));
        break;
      }
      case Opcode::SetGlobal: {
        executeSetGlobal(chunk, chunk.readByte(frame.ip++));
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
        recordLoopBackedge(*frame.closure->function);
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
        executeGetUpvalue(frame, chunk.readByte(frame.ip++));
        break;
      }
      case Opcode::SetUpvalue: {
        executeSetUpvalue(frame, chunk.readByte(frame.ip++));
        break;
      }
      case Opcode::CloseUpvalue: {
        executeCloseUpvalue();
        break;
      }
      case Opcode::MethodCall: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();
        const std::uint8_t argCount = chunk.readByte(frame.ip++);
        const std::uint8_t feedbackSlotIndex = chunk.readByte(frame.ip++);
        if (stack_.size() < static_cast<std::size_t>(argCount) + 1) {
          ++propertyInlineCacheStats_.methodCallDispatchErrors;
          throw RuntimeError("method call stack underflow");
        }

        const std::size_t receiverIndex = stack_.size() - argCount - 1;
        Value receiver = stack_[receiverIndex];
        auto callMethod = [&](ObjClosure* method, std::size_t slotStart, const std::string& label) {
          try {
            callBytecodeClosure(method, argCount, receiverIndex, slotStart, label);
          } catch (const RuntimeError&) {
            ++propertyInlineCacheStats_.methodCallDispatchErrors;
            throw;
          }
        };

        if (isGcInstanceValue(receiver)) {
          ObjClass* klass = gcInstanceClass(receiver);
          FeedbackSlot* feedback =
              inlineCachesEnabled_ ? &chunk.feedbackSlot(feedbackSlotIndex) : nullptr;
          if (feedback != nullptr && feedback->state != FeedbackState::Megamorphic) {
            if (const auto* entry = findMethodInlineCacheEntry(feedback->method, klass, false)) {
              ++propertyInlineCacheStats_.methodCallHits;
              callMethod(entry->method, receiverIndex, "method");
              ++propertyInlineCacheStats_.methodCallInstanceDispatches;
              break;
            }
          }

          if (feedback != nullptr && feedback->state == FeedbackState::Megamorphic) {
            ++propertyInlineCacheStats_.methodCallBypasses;
          } else {
            ++propertyInlineCacheStats_.methodCallMisses;
          }
          auto method = findMethod(klass, name);
          if (method == nullptr) {
            ++propertyInlineCacheStats_.methodCallDispatchErrors;
            throw RuntimeError("value has no method: " + name);
          }

          if (feedback != nullptr && updateMethodFeedback(*feedback, klass, method, false)) {
            ++propertyInlineCacheStats_.methodCallUpdates;
          }
          callMethod(method, receiverIndex, "method");
          ++propertyInlineCacheStats_.methodCallInstanceDispatches;
          break;
        }

        if (receiver.isGcClass()) {
          ObjClass* klass = receiver.asGcClass();
          FeedbackSlot* feedback =
              inlineCachesEnabled_ ? &chunk.feedbackSlot(feedbackSlotIndex) : nullptr;
          if (feedback != nullptr && feedback->state != FeedbackState::Megamorphic) {
            if (const auto* entry = findMethodInlineCacheEntry(feedback->method, klass, true)) {
              ++propertyInlineCacheStats_.methodCallHits;
              callMethod(entry->method, receiverIndex + 1, "function");
              ++propertyInlineCacheStats_.methodCallStaticDispatches;
              break;
            }
          }

          if (feedback != nullptr && feedback->state == FeedbackState::Megamorphic) {
            ++propertyInlineCacheStats_.methodCallBypasses;
          } else {
            ++propertyInlineCacheStats_.methodCallMisses;
          }
          auto method = findStaticMethod(klass, name);
          if (method == nullptr) {
            ++propertyInlineCacheStats_.methodCallDispatchErrors;
            throw RuntimeError("value has no method: " + name);
          }

          if (feedback != nullptr && updateMethodFeedback(*feedback, klass, method, true)) {
            ++propertyInlineCacheStats_.methodCallUpdates;
          }
          callMethod(method, receiverIndex + 1, "function");
          ++propertyInlineCacheStats_.methodCallStaticDispatches;
          break;
        }

        if (!receiver.isGcArray()) {
          ++propertyInlineCacheStats_.methodCallDispatchErrors;
          throw RuntimeError("method call receiver is not an array");
        }

        std::vector<Value>& array = receiver.asGcArray()->elements;
        ++propertyInlineCacheStats_.methodCallBypasses;

        if (name == "push") {
          if (argCount != 1) {
            ++propertyInlineCacheStats_.methodCallDispatchErrors;
            throw RuntimeError("push expects 1 argument");
          }

          ++propertyInlineCacheStats_.methodCallArrayPushes;
          array.push_back(stack_[receiverIndex + 1]);
          stack_.resize(receiverIndex);
          push(Value(static_cast<double>(array.size())));
          break;
        }

        if (name == "pop") {
          if (argCount != 0) {
            ++propertyInlineCacheStats_.methodCallDispatchErrors;
            throw RuntimeError("pop expects 0 arguments");
          }

          ++propertyInlineCacheStats_.methodCallArrayPops;
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

        ++propertyInlineCacheStats_.methodCallDispatchErrors;
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
        executeArrayLiteral(chunk.readByte(frame.ip++));
        break;
      }
      case Opcode::GetIndex: {
        executeGetIndex();
        break;
      }
      case Opcode::SetIndex: {
        executeSetIndex();
        break;
      }
      case Opcode::Object: {
        executeObjectLiteral(chunk, chunk.readByte(frame.ip++));
        break;
      }
      case Opcode::GetProperty: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        executeGetProperty(chunk, nameIndex, chunk.readByte(frame.ip++));
        break;
      }
      case Opcode::SetProperty: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        executeSetProperty(chunk, nameIndex, chunk.readByte(frame.ip++));
        break;
      }
      case Opcode::GetCurrentClosure:
        executeGetCurrentClosure(frame);
        break;
    }
  }
}

std::size_t VM::objectCount() const {
  return heapObjectCount_ - internalObjectCount_;
}

PropertyInlineCacheStats VM::propertyInlineCacheStats() const {
  return propertyInlineCacheStats_;
}

void VM::resetPropertyInlineCacheStats() {
  propertyInlineCacheStats_ = PropertyInlineCacheStats{};
}

bool VM::inlineCachesEnabled() const {
  return inlineCachesEnabled_;
}

void VM::setInlineCachesEnabled(bool enabled) {
  inlineCachesEnabled_ = enabled;
}

bool VM::jitEnabled() const {
  return jitOptions_.enabled;
}

void VM::setJitEnabled(bool enabled) {
  jitOptions_.enabled = enabled;
}

void VM::setJitCallThreshold(std::uint32_t threshold) {
  jitOptions_.callThreshold = threshold;
}

void VM::setJitBackedgeThreshold(std::uint32_t threshold) {
  jitOptions_.backedgeThreshold = threshold;
}

#ifdef MINIJS_TESTING
const ObjShape* VM::debugGlobalObjectShape(const std::string& name) const {
  auto global = globals_.find(name);
  if (global == globals_.end() || !global->second.isGcObject()) {
    return nullptr;
  }
  return global->second.asGcObject()->shape;
}

bool VM::debugGlobalObjectUsesDictionary(const std::string& name) const {
  auto global = globals_.find(name);
  if (global == globals_.end() || !global->second.isGcObject()) {
    return false;
  }
  return global->second.asGcObject()->dictionaryMode;
}

const JitFeedback* VM::debugGlobalFunctionJitFeedback(const std::string& name) const {
  auto global = globals_.find(name);
  if (global == globals_.end() || !global->second.isGcClosure()) {
    return nullptr;
  }
  return &global->second.asGcClosure()->function->function.jit;
}

const JitFeedback* VM::debugGlobalClassMethodJitFeedback(const std::string& className,
                                                         const std::string& methodName,
                                                         bool isStatic) const {
  auto global = globals_.find(className);
  if (global == globals_.end() || !global->second.isGcClass()) {
    return nullptr;
  }

  ObjClass* klass = global->second.asGcClass();
  const auto& methods = isStatic ? klass->staticMethods : klass->methods;
  auto method = methods.find(methodName);
  if (method == methods.end() || method->second == nullptr) {
    return nullptr;
  }
  return &method->second->function->function.jit;
}

const BaselineCode* VM::debugGlobalFunctionBaselineCode(const std::string& name) const {
  const JitFeedback* feedback = debugGlobalFunctionJitFeedback(name);
  if (feedback == nullptr) {
    return nullptr;
  }
  return feedback->baselineCode.get();
}

std::string VM::debugGlobalFunctionJitCompileError(const std::string& name) const {
  const JitFeedback* feedback = debugGlobalFunctionJitFeedback(name);
  if (feedback == nullptr) {
    return "";
  }
  return feedback->compileError;
}

Value VM::debugExecuteGlobalFunctionBaseline(const std::string& name,
                                             const std::vector<Value>& arguments) {
  auto global = globals_.find(name);
  if (global == globals_.end() || !global->second.isGcClosure()) {
    throw RuntimeError("global function not found: " + name);
  }

  ObjClosure* closure = global->second.asGcClosure();
  BytecodeFunction& function = closure->function->function;
  if (arguments.size() != function.params.size()) {
    throw RuntimeError("function " + function.name + " expects " +
                       std::to_string(function.params.size()) + " arguments");
  }
  if (function.jit.baselineCode == nullptr) {
    throw RuntimeError("global function has no baseline code: " + name);
  }

  stack_.clear();
  frames_.clear();
  openUpvalues_.clear();
  temporaryRoots_.clear();

  push(Value(closure));
  for (const Value& argument : arguments) {
    push(argument);
  }

  frames_.push_back(CallFrame{closure, 0, 0, 1});
  Value result;
  try {
    result = executeBaselineCode(*function.jit.baselineCode, frames_.back());
  } catch (...) {
    stack_.clear();
    frames_.clear();
    openUpvalues_.clear();
    temporaryRoots_.clear();
    throw;
  }
  frames_.pop_back();

  stack_.resize(0);
  push(result);
  Value copied = copyOutValue(stack_.back());
  stack_.clear();
  temporaryRoots_.clear();
  return copied;
}

Value VM::debugExecuteGlobalFunctionBaselineEntry(const std::string& name,
                                                  const std::vector<Value>& arguments,
                                                  BaselineEntry entry) {
  if (entry == nullptr) {
    throw RuntimeError("baseline entry is null");
  }

  auto global = globals_.find(name);
  if (global == globals_.end() || !global->second.isGcClosure()) {
    throw RuntimeError("global function not found: " + name);
  }

  ObjClosure* closure = global->second.asGcClosure();
  BytecodeFunction& function = closure->function->function;
  if (arguments.size() != function.params.size()) {
    throw RuntimeError("function " + function.name + " expects " +
                       std::to_string(function.params.size()) + " arguments");
  }

  stack_.clear();
  frames_.clear();
  openUpvalues_.clear();
  temporaryRoots_.clear();

  push(Value(closure));
  for (const Value& argument : arguments) {
    push(argument);
  }

  BaselineFrame frame;
  frame.vm = this;
  frame.closure = closure;
  frame.returnSlot = 0;
  frame.slotStart = 1;

  try {
    entry(&frame);
  } catch (...) {
    stack_.clear();
    frames_.clear();
    openUpvalues_.clear();
    temporaryRoots_.clear();
    throw;
  }

  if (frame.failed) {
    stack_.clear();
    frames_.clear();
    openUpvalues_.clear();
    temporaryRoots_.clear();
    throw RuntimeError("baseline runtime helper failed");
  }
  if (!frame.completed) {
    stack_.clear();
    frames_.clear();
    openUpvalues_.clear();
    temporaryRoots_.clear();
    throw RuntimeError("baseline entry did not complete");
  }
  if (stack_.empty()) {
    stack_.clear();
    temporaryRoots_.clear();
    throw RuntimeError("baseline entry produced no result");
  }

  Value copied = copyOutValue(stack_.back());
  stack_.clear();
  temporaryRoots_.clear();
  return copied;
}

PropertyInlineCacheStats VM::debugPropertyInlineCacheStats() const {
  return propertyInlineCacheStats();
}

void VM::debugResetPropertyInlineCacheStats() {
  resetPropertyInlineCacheStats();
}
#endif

std::size_t VM::objectPropertyCount(const ObjObject& object) const {
  if (object.dictionaryMode) {
    return object.dictionary.size();
  }
  return object.slots.size();
}

bool VM::objectHasProperty(const ObjObject& object, const std::string& name) const {
  if (object.dictionaryMode) {
    return object.dictionary.find(name) != object.dictionary.end();
  }
  return object.shape->slots.find(name) != object.shape->slots.end();
}

Value VM::objectGetProperty(const ObjObject& object, const std::string& name) const {
  if (object.dictionaryMode) {
    auto property = object.dictionary.find(name);
    if (property == object.dictionary.end()) {
      return Value::undefined();
    }
    return property->second;
  }

  auto slot = object.shape->slots.find(name);
  if (slot == object.shape->slots.end()) {
    return Value::undefined();
  }
  return object.slots[slot->second];
}

void VM::objectSetProperty(ObjObject& object, const std::string& name, Value value) {
  if (object.dictionaryMode) {
    object.dictionary[name] = std::move(value);
    return;
  }

  auto slot = object.shape->slots.find(name);
  if (slot != object.shape->slots.end()) {
    object.slots[slot->second] = std::move(value);
    return;
  }

  object.shape = transitionObjectShape(object.shape, name);
  object.slots.push_back(std::move(value));
}

bool VM::objectDeleteProperty(ObjObject& object, const std::string& name) {
  objectMaterializeDictionary(object);
  return object.dictionary.erase(name) > 0;
}

std::vector<std::string> VM::objectKeys(const ObjObject& object) const {
  std::vector<std::string> keys;
  if (object.dictionaryMode) {
    keys.reserve(object.dictionary.size());
    for (const auto& property : object.dictionary) {
      keys.push_back(property.first);
    }
    return keys;
  }

  keys.reserve(object.shape->slots.size());
  for (const auto& slot : object.shape->slots) {
    keys.push_back(slot.first);
  }
  return keys;
}

ObjShape* VM::transitionObjectShape(ObjShape* shape, const std::string& name) {
  auto existing = shape->transitions.find(name);
  if (existing != shape->transitions.end()) {
    return existing->second;
  }

  auto* next = allocateInternalObject<ObjShape>(shape, name);
  next->slots = shape->slots;
  next->slots[name] = next->slots.size();
  shape->transitions[name] = next;
  return next;
}

void VM::objectMaterializeDictionary(ObjObject& object) const {
  if (object.dictionaryMode) {
    return;
  }

  object.dictionary.clear();
  for (const auto& slot : object.shape->slots) {
    object.dictionary[slot.first] = object.slots[slot.second];
  }
  object.dictionaryMode = true;
  object.shape = rootObjectShape_;
  object.slots.clear();
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
    for (const std::string& key : objectKeys(*value.asGcObject())) {
      properties[key] = copyOutValue(objectGetProperty(*value.asGcObject(), key));
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

void VM::validateBaselineRuntimeFrame(const BaselineFrame& frame) const {
  if (frame.vm != this || frame.closure == nullptr) {
    throw RuntimeError("invalid baseline frame");
  }
  if (frame.failed) {
    throw RuntimeError("baseline frame already failed");
  }
  if (frame.completed) {
    throw RuntimeError("baseline frame already completed");
  }
}

bool VM::baselineRuntimePush(BaselineFrame& frame, const Value& value) {
  validateBaselineRuntimeFrame(frame);
  push(value);
  return true;
}

bool VM::baselineRuntimePushConstant(BaselineFrame& frame, std::uint32_t constantIndex) {
  validateBaselineRuntimeFrame(frame);
  const Chunk& chunk = frame.closure->function->function.chunk;
  const Value& constant = chunk.constant(constantIndex);
  if (constant.isString()) {
    push(makeGcString(constant.asString()));
  } else {
    push(constant);
  }
  return true;
}

bool VM::baselineRuntimeGetLocal(BaselineFrame& frame, std::uint32_t slot) {
  validateBaselineRuntimeFrame(frame);
  const std::size_t absoluteSlot = frame.slotStart + slot;
  if (absoluteSlot >= stack_.size()) {
    throw RuntimeError("local slot out of bounds");
  }
  push(stack_[absoluteSlot]);
  return true;
}

bool VM::baselineRuntimeSetLocal(BaselineFrame& frame, std::uint32_t slot) {
  validateBaselineRuntimeFrame(frame);
  const std::size_t absoluteSlot = frame.slotStart + slot;
  if (absoluteSlot >= stack_.size()) {
    throw RuntimeError("local slot out of bounds");
  }
  stack_[absoluteSlot] = peek();
  return true;
}

bool VM::baselineRuntimePop(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  pop();
  return true;
}

bool VM::baselineRuntimeAdd(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  Value right = pop();
  Value left = pop();
  if (left.isGcString() || right.isGcString()) {
    push(makeGcString(left.toString() + right.toString()));
  } else {
    push(Value(left.asNumber() + right.asNumber()));
  }
  return true;
}

bool VM::baselineRuntimeSub(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  Value right = pop();
  Value left = pop();
  push(Value(left.asNumber() - right.asNumber()));
  return true;
}

bool VM::baselineRuntimeMul(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  Value right = pop();
  Value left = pop();
  push(Value(left.asNumber() * right.asNumber()));
  return true;
}

bool VM::baselineRuntimeDiv(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  Value right = pop();
  Value left = pop();
  const double divisor = checkedDivisor(right, "division by zero");
  push(Value(left.asNumber() / divisor));
  return true;
}

bool VM::baselineRuntimeMod(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  Value right = pop();
  Value left = pop();
  const double divisor = checkedDivisor(right, "modulo by zero");
  push(Value(std::fmod(left.asNumber(), divisor)));
  return true;
}

bool VM::baselineRuntimeNegate(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  push(Value(-pop().asNumber()));
  return true;
}

bool VM::baselineRuntimeReturn(BaselineFrame& frame) {
  validateBaselineRuntimeFrame(frame);
  Value result = pop();
  if (frame.returnSlot > stack_.size()) {
    throw RuntimeError("baseline return slot out of bounds");
  }
  closeUpvalues(frame.slotStart);
  stack_.resize(frame.returnSlot);
  push(result);
  frame.completed = true;
  return true;
}

void VM::executeDefineGlobal(const Chunk& chunk, std::size_t nameIndex) {
  const std::string name = chunk.constant(nameIndex).asString();
  globals_[name] = pop();
}

void VM::executeGetGlobal(const Chunk& chunk, std::size_t nameIndex) {
  const std::string name = chunk.constant(nameIndex).asString();
  auto it = globals_.find(name);
  if (it == globals_.end()) {
    throw RuntimeError("undefined variable: " + name);
  }
  push(it->second);
}

void VM::executeSetGlobal(const Chunk& chunk, std::size_t nameIndex) {
  const std::string name = chunk.constant(nameIndex).asString();
  auto it = globals_.find(name);
  if (it == globals_.end()) {
    throw RuntimeError("undefined variable: " + name);
  }
  it->second = peek();
}

void VM::executeArrayLiteral(std::size_t count) {
  collectGarbageIfNeeded();

  std::vector<Value> elements(count);
  for (std::size_t i = count; i > 0; --i) {
    elements[i - 1] = pop();
  }
  push(Value(allocateObject<ObjArray>(std::move(elements))));
}

void VM::executeGetIndex() {
  Value index = pop();
  Value array = pop();
  if (!array.isGcArray()) {
    throw RuntimeError("value is not an array");
  }
  const std::vector<Value>& elements = array.asGcArray()->elements;
  push(elements[arrayIndexFromValue(index, elements.size())]);
}

void VM::executeSetIndex() {
  Value value = pop();
  Value index = pop();
  Value array = pop();
  if (!array.isGcArray()) {
    throw RuntimeError("value is not an array");
  }
  std::vector<Value>& elements = array.asGcArray()->elements;
  elements[arrayIndexFromValue(index, elements.size())] = value;
  push(value);
}

void VM::executeObjectLiteral(const Chunk& chunk, std::size_t namesIndex) {
  const std::vector<Value>& names = chunk.constant(namesIndex).asArray();

  if (stack_.size() < names.size()) {
    throw RuntimeError("bytecode stack underflow");
  }

  auto* object = allocateObject<ObjObject>(rootObjectShape_);
  TemporaryRootScope roots(*this);
  roots.add(object);

  const std::size_t valueStart = stack_.size() - names.size();
  for (std::size_t i = 0; i < names.size(); ++i) {
    const std::string& name = names[i].asString();
    objectSetProperty(*object, name, stack_[valueStart + i]);
  }

  for (std::size_t i = 0; i < names.size(); ++i) {
    pop();
  }

  push(Value(object));
}

void VM::executeGetProperty(const Chunk& chunk, std::size_t nameIndex,
                            std::size_t feedbackSlotIndex) {
  const std::string& name = chunk.constant(nameIndex).asString();

  Value object = pop();

  if (object.isGcArray()) {
    if (name == "length") {
      push(Value(static_cast<double>(object.asGcArray()->elements.size())));
      return;
    }

    push(Value::undefined());
    return;
  }

  if (object.isGcString()) {
    if (name == "length") {
      push(Value(static_cast<double>(object.asGcString()->value.size())));
      return;
    }

    push(Value::undefined());
    return;
  }

  if (isGcInstanceValue(object)) {
    auto& fields = gcInstanceFields(object);
    auto field = fields.find(name);
    if (field != fields.end()) {
      push(field->second);
      return;
    }

    auto method = findMethod(gcInstanceClass(object), name);
    if (method != nullptr) {
      push(object);
      auto* boundMethod = allocateObject<ObjBoundMethod>(object, method);
      pop();
      push(Value(boundMethod));
      return;
    }

    push(Value::undefined());
    return;
  }

  if (object.isGcClass()) {
    auto method = findStaticMethod(object.asGcClass(), name);
    if (method != nullptr) {
      push(Value(method));
      return;
    }

    push(Value::undefined());
    return;
  }

  if (!object.isGcObject()) {
    throw RuntimeError("value is not an object");
  }

  ObjObject* objectValue = object.asGcObject();
  if (objectValue->dictionaryMode) {
    ++propertyInlineCacheStats_.bypasses;
    push(objectGetProperty(*objectValue, name));
    return;
  }

  FeedbackSlot* feedback =
      inlineCachesEnabled_ ? &chunk.feedbackSlot(feedbackSlotIndex) : nullptr;
  if (feedback != nullptr && feedback->state != FeedbackState::Megamorphic) {
    if (const auto* entry = findPropertyInlineCacheEntry(feedback->property, *objectValue)) {
      ++propertyInlineCacheStats_.hits;
      push(objectValue->slots[entry->slot]);
      return;
    }
  }

  if (feedback != nullptr && feedback->state == FeedbackState::Megamorphic) {
    ++propertyInlineCacheStats_.bypasses;
  } else {
    ++propertyInlineCacheStats_.misses;
  }
  Value result = objectGetProperty(*objectValue, name);
  auto slot = objectValue->shape->slots.find(name);
  if (feedback != nullptr && slot != objectValue->shape->slots.end() &&
      slot->second < objectValue->slots.size()) {
    if (updatePropertyFeedback(*feedback, objectValue->shape, slot->second)) {
      ++propertyInlineCacheStats_.updates;
    }
  }

  push(result);
}

void VM::executeSetProperty(const Chunk& chunk, std::size_t nameIndex,
                            std::size_t feedbackSlotIndex) {
  const std::string& name = chunk.constant(nameIndex).asString();

  if (stack_.size() < 2) {
    throw RuntimeError("bytecode stack underflow");
  }

  Value value = peek();
  Value object = stack_[stack_.size() - 2];

  auto finishAssignment = [&]() {
    pop();
    pop();
    push(value);
  };

  if (isGcInstanceValue(object)) {
    gcInstanceFields(object)[name] = value;
    finishAssignment();
    return;
  }

  if (!object.isGcObject()) {
    throw RuntimeError("value is not an object");
  }

  ObjObject* objectValue = object.asGcObject();
  if (objectValue->dictionaryMode) {
    ++propertyInlineCacheStats_.setBypasses;
    objectSetProperty(*objectValue, name, value);
    finishAssignment();
    return;
  }

  FeedbackSlot* feedback =
      inlineCachesEnabled_ ? &chunk.feedbackSlot(feedbackSlotIndex) : nullptr;
  if (feedback != nullptr && feedback->state != FeedbackState::Megamorphic) {
    if (const auto* entry = findPropertyInlineCacheEntry(feedback->property, *objectValue)) {
      ++propertyInlineCacheStats_.setHits;
      objectValue->slots[entry->slot] = value;
      finishAssignment();
      return;
    }
  }

  if (feedback != nullptr && feedback->state == FeedbackState::Megamorphic) {
    ++propertyInlineCacheStats_.setBypasses;
  } else {
    ++propertyInlineCacheStats_.setMisses;
  }

  auto slot = objectValue->shape->slots.find(name);
  if (slot != objectValue->shape->slots.end() && slot->second < objectValue->slots.size()) {
    objectValue->slots[slot->second] = value;
    if (feedback != nullptr &&
        updatePropertyFeedback(*feedback, objectValue->shape, slot->second)) {
      ++propertyInlineCacheStats_.setUpdates;
    }
    finishAssignment();
    return;
  }

  objectSetProperty(*objectValue, name, value);

  slot = objectValue->shape->slots.find(name);
  if (feedback != nullptr && slot != objectValue->shape->slots.end() &&
      slot->second < objectValue->slots.size()) {
    if (updatePropertyFeedback(*feedback, objectValue->shape, slot->second)) {
      ++propertyInlineCacheStats_.setUpdates;
    }
  }

  finishAssignment();
}

void VM::executeGetUpvalue(const CallFrame& frame, std::size_t slot) {
  const auto& upvalue = frame.closure->upvalues[slot];
  push(upvalue->isClosed ? upvalue->closed : stack_[upvalue->stackIndex]);
}

void VM::executeSetUpvalue(const CallFrame& frame, std::size_t slot) {
  const auto& upvalue = frame.closure->upvalues[slot];
  if (upvalue->isClosed) {
    upvalue->closed = peek();
  } else {
    stack_[upvalue->stackIndex] = peek();
  }
}

void VM::executeCloseUpvalue() {
  closeUpvalues(stack_.size() - 1);
  pop();
}

void VM::executeGetCurrentClosure(const CallFrame& frame) {
  push(Value(frame.closure));
}

void VM::callBytecodeClosure(ObjClosure* closure, std::size_t argCount, std::size_t returnSlot,
                             std::size_t slotStart, const std::string& label,
                             bool returnsReceiver) {
  BytecodeFunction& function = closure->function->function;
  if (argCount != function.params.size()) {
    throw RuntimeError(label + " " + function.name + " expects " +
                       std::to_string(function.params.size()) + " arguments");
  }

  const bool shouldEnterBaseline =
      jitOptions_.enabled && function.jit.state == JitState::Compiled &&
      function.jit.baselineCode != nullptr;

  recordFunctionCall(*closure->function);

  if (shouldEnterBaseline) {
    frames_.push_back(CallFrame{
        closure,
        0,
        returnSlot,
        slotStart,
        returnsReceiver,
    });

    Value result;
    try {
      incrementSaturating(function.jit.baselineEntryCount);
      result = executeBaselineCode(*function.jit.baselineCode, frames_.back());
    } catch (...) {
      frames_.pop_back();
      throw;
    }

    frames_.pop_back();
    stack_.resize(returnSlot);
    push(result);
    return;
  }

  frames_.push_back(CallFrame{
      closure,
      0,
      returnSlot,
      slotStart,
      returnsReceiver,
  });
}

void VM::recordFunctionCall(ObjFunction& function) {
  if (!jitOptions_.enabled) {
    return;
  }

  incrementSaturating(function.function.jit.callCount);
  maybeScheduleJit(function.function);
}

void VM::recordLoopBackedge(ObjFunction& function) {
  if (!jitOptions_.enabled) {
    return;
  }

  incrementSaturating(function.function.jit.backedgeCount);
  maybeScheduleJit(function.function);
}

void VM::maybeScheduleJit(BytecodeFunction& function) {
  JitFeedback& jit = function.jit;
  if (jit.state != JitState::Cold) {
    return;
  }

  if (jit.callCount >= jitOptions_.callThreshold ||
      jit.backedgeCount >= jitOptions_.backedgeThreshold) {
    jit.state = JitState::Scheduled;
    compileScheduledJit(function);
  }
}

void VM::compileScheduledJit(BytecodeFunction& function) {
  JitFeedback& jit = function.jit;
  if (jit.state != JitState::Scheduled) {
    return;
  }

  BaselineCompileResult result = compileBaseline(function);
  if (result.succeeded()) {
    jit.baselineCode = std::move(result.code);
    jit.compileError.clear();
    jit.state = JitState::Compiled;
    return;
  }

  jit.baselineCode.reset();
  jit.compileError = std::move(result.error);
  jit.state = JitState::Failed;
}

Value VM::executeBaselineCode(const BaselineCode& code, CallFrame& frame) {
  const Chunk& chunk = frame.closure->function->function.chunk;
  std::size_t ip = 0;
  auto instructionIndexForTarget = [&](std::size_t bytecodeOffset) -> std::size_t {
    if (bytecodeOffset >= code.bytecodeOffsetToInstructionIndex.size()) {
      throw RuntimeError("baseline jump target is out of bounds");
    }
    const std::size_t target = code.bytecodeOffsetToInstructionIndex[bytecodeOffset];
    if (target == kInvalidInstructionIndex || target >= code.instructions.size()) {
      throw RuntimeError("baseline jump target is not an instruction");
    }
    return target;
  };

  while (ip < code.instructions.size()) {
    const DecodedInstruction& instruction = code.instructions[ip];

    switch (instruction.opcode) {
      case Opcode::Constant: {
        const Value& constant = chunk.constant(instruction.operands[0]);
        if (constant.isString()) {
          push(makeGcString(constant.asString()));
        } else {
          push(constant);
        }
        ++ip;
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
        ++ip;
        break;
      }
      case Opcode::Sub: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() - right.asNumber()));
        ++ip;
        break;
      }
      case Opcode::Mul: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() * right.asNumber()));
        ++ip;
        break;
      }
      case Opcode::Div: {
        Value right = pop();
        Value left = pop();
        const double divisor = checkedDivisor(right, "division by zero");
        push(Value(left.asNumber() / divisor));
        ++ip;
        break;
      }
      case Opcode::Mod: {
        Value right = pop();
        Value left = pop();
        const double divisor = checkedDivisor(right, "modulo by zero");
        push(Value(std::fmod(left.asNumber(), divisor)));
        ++ip;
        break;
      }
      case Opcode::Negate:
        push(Value(-pop().asNumber()));
        ++ip;
        break;
      case Opcode::Return: {
        Value result = pop();
        if (frame.returnsReceiver) {
          result = stack_[frame.slotStart];
        }
        closeUpvalues(frame.slotStart);
        return result;
      }
      case Opcode::GetLocal: {
        const std::size_t absoluteSlot = frame.slotStart + instruction.operands[0];
        if (absoluteSlot >= stack_.size()) {
          throw RuntimeError("local slot out of bounds");
        }
        push(stack_[absoluteSlot]);
        ++ip;
        break;
      }
      case Opcode::SetLocal: {
        const std::size_t absoluteSlot = frame.slotStart + instruction.operands[0];
        if (absoluteSlot >= stack_.size()) {
          throw RuntimeError("local slot out of bounds");
        }
        stack_[absoluteSlot] = peek();
        ++ip;
        break;
      }
      case Opcode::Pop:
        pop();
        ++ip;
        break;
      case Opcode::Equal: {
        Value right = pop();
        Value left = pop();
        push(Value(left.equals(right)));
        ++ip;
        break;
      }
      case Opcode::Greater: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() > right.asNumber()));
        ++ip;
        break;
      }
      case Opcode::Less: {
        Value right = pop();
        Value left = pop();
        push(Value(left.asNumber() < right.asNumber()));
        ++ip;
        break;
      }
      case Opcode::Not:
        push(Value(!pop().isTruthy()));
        ++ip;
        break;
      case Opcode::JumpIfFalse:
        ip = peek().isTruthy() ? ip + 1 : instructionIndexForTarget(instruction.jumpTarget);
        break;
      case Opcode::Jump:
        ip = instructionIndexForTarget(instruction.jumpTarget);
        break;
      case Opcode::Loop:
        recordLoopBackedge(*frame.closure->function);
        ip = instructionIndexForTarget(instruction.jumpTarget);
        break;

      case Opcode::DefineGlobal:
        executeDefineGlobal(chunk, instruction.operands[0]);
        ++ip;
        break;
      case Opcode::GetGlobal:
        executeGetGlobal(chunk, instruction.operands[0]);
        ++ip;
        break;
      case Opcode::SetGlobal:
        executeSetGlobal(chunk, instruction.operands[0]);
        ++ip;
        break;
      case Opcode::Array:
        executeArrayLiteral(instruction.operands[0]);
        ++ip;
        break;
      case Opcode::GetIndex:
        executeGetIndex();
        ++ip;
        break;
      case Opcode::SetIndex:
        executeSetIndex();
        ++ip;
        break;
      case Opcode::Object:
        executeObjectLiteral(chunk, instruction.operands[0]);
        ++ip;
        break;
      case Opcode::GetProperty:
        executeGetProperty(chunk, instruction.operands[0], instruction.operands[1]);
        ++ip;
        break;
      case Opcode::SetProperty:
        executeSetProperty(chunk, instruction.operands[0], instruction.operands[1]);
        ++ip;
        break;
      case Opcode::GetUpvalue:
        executeGetUpvalue(frame, instruction.operands[0]);
        ++ip;
        break;
      case Opcode::SetUpvalue:
        executeSetUpvalue(frame, instruction.operands[0]);
        ++ip;
        break;
      case Opcode::CloseUpvalue:
        executeCloseUpvalue();
        ++ip;
        break;
      case Opcode::GetCurrentClosure:
        executeGetCurrentClosure(frame);
        ++ip;
        break;

      case Opcode::Call:
      case Opcode::MethodCall:
      case Opcode::Closure:
      case Opcode::Class:
      case Opcode::Method:
      case Opcode::StaticMethod:
      case Opcode::Inherit:
      case Opcode::SuperCall:
        throw RuntimeError(std::string("baseline cannot execute ") + opcodeName(instruction.opcode));
    }
  }

  throw RuntimeError("baseline code reached end without return");
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
  markObject(rootObjectShape_);

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
  if (heapObjectCount_ + 1 <= nextGcObjectCount_) {
    return;
  }

  collectGarbage();
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
      markObject(objectValue->shape);
      for (const Value& slot : objectValue->slots) {
        markValue(slot);
      }
      for (const auto& property : objectValue->dictionary) {
        markValue(property.second);
      }
      break;
    }
    case ObjType::Function: {
      auto* function = static_cast<ObjFunction*>(object);

      for (const Value& constant : function->function.chunk.constants()) {
        markValue(constant);
      }

      for (const FeedbackSlot& feedbackSlot : function->function.chunk.feedbackSlots()) {
        const PropertyInlineCache& propertyCache = feedbackSlot.property;
        for (std::size_t index = 0; index < propertyCache.size; ++index) {
          markObject(propertyCache.entries[index].shape);
        }

        const MethodInlineCache& methodCache = feedbackSlot.method;
        for (std::size_t index = 0; index < methodCache.size; ++index) {
          const MethodInlineCacheEntry& entry = methodCache.entries[index];
          markObject(entry.klass);
          markClosure(entry.method);
        }
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
    case ObjType::Shape: {
      auto* shape = static_cast<ObjShape*>(object);
      markObject(shape->parent);
      for (const auto& transition : shape->transitions) {
        markObject(transition.second);
      }
      break;
    }
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
    --heapObjectCount_;
    delete unreached;
  }
}

void VM::collectGarbage() {
  markRoots();
  sweep();
  nextGcObjectCount_ = std::max<std::size_t>(heapObjectCount_ * 2, 8);
}

}  // namespace minijs
