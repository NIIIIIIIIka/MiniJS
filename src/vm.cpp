#include "minijs/vm.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

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

std::shared_ptr<BytecodeClosure> findMethod(const std::shared_ptr<BytecodeClass>& klass,
                                            const std::string& name) {
  for (auto current = klass; current != nullptr; current = current->superclass) {
    const auto method = current->methods.find(name);
    if (method != current->methods.end()) {
      return method->second;
    }
  }
  return nullptr;
}

std::shared_ptr<BytecodeClosure> findStaticMethod(const std::shared_ptr<BytecodeClass>& klass,
                                                  const std::string& name) {
  for (auto current = klass; current != nullptr; current = current->superclass) {
    const auto method = current->staticMethods.find(name);
    if (method != current->staticMethods.end()) {
      return method->second;
    }
  }
  return nullptr;
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

    if (value.isString()) {
      return Value(static_cast<double>(value.asString().size()));
    }

    if (value.isArray()) {
      return Value(static_cast<double>(value.asArray().size()));
    }

    if (value.isObject()) {
      return Value(static_cast<double>(value.asObject().size()));
    }

    if (value.isBytecodeInstance()) {
      return Value(static_cast<double>(value.asBytecodeInstance()->fields.size()));
    }

    throw RuntimeError("len expects string, array, object, or instance");
  });

  defineBuiltin("typeOf", 1, [](const std::vector<Value>& arguments) -> Value {
    const Value& value = arguments[0];

    if (value.isNumber()) {
      return Value(std::string("number"));
    }
    if (value.isBoolean()) {
      return Value(std::string("boolean"));
    }
    if (value.isString()) {
      return Value(std::string("string"));
    }
    if (value.isArray()) {
      return Value(std::string("array"));
    }
    if (value.isObject()) {
      return Value(std::string("object"));
    }
    if (value.isBytecodeClass()) {
      return Value(std::string("class"));
    }
    if (value.isBytecodeInstance()) {
      return Value(std::string("instance"));
    }
    if (value.isBytecodeFunction() || value.isBytecodeClosure() || value.isFunction()) {
      return Value(std::string("function"));
    }
    if (value.isNativeFunction()) {
      return Value(std::string("builtin"));
    }
    if (value.isNull()) {
      return Value(std::string("null"));
    }
    if (value.isUndefined()) {
      return Value(std::string("undefined"));
    }

    return Value(std::string("unknown"));
  });

  defineBuiltin("has", 2, [](const std::vector<Value>& arguments) -> Value {
    const Value& object = arguments[0];
    const Value& key = arguments[1];

    if (!key.isString()) {
      throw RuntimeError("has key must be a string");
    }

    const std::string& name = key.asString();

    if (object.isObject()) {
      return Value(object.asObject().find(name) != object.asObject().end());
    }
    if (object.isBytecodeInstance()) {
      const auto& fields = object.asBytecodeInstance()->fields;
      return Value(fields.find(name) != fields.end());
    }
    if (object.isArray()) {
      return Value(name == "length" || name == "push" || name == "pop");
    }
    if (object.isString()) {
      return Value(name == "length");
    }

    return Value(false);
  });

  defineBuiltin("del", 2, [](const std::vector<Value>& arguments) -> Value {
    Value object = arguments[0];
    const Value& key = arguments[1];

    if (!key.isString()) {
      throw RuntimeError("del key must be a string");
    }

    const std::string& name = key.asString();
    if (object.isBytecodeInstance()) {
      return Value(object.asBytecodeInstance()->fields.erase(name) > 0);
    }
    if (!object.isObject()) {
      return Value(false);
    }

    return Value(object.asObject().erase(name) > 0);
  });

  defineBuiltin("keys", 1, [](const std::vector<Value>& arguments) -> Value {
    const Value& object = arguments[0];
    std::vector<Value> keys;

    if (object.isObject()) {
      for (const auto& key : object.asObject()) {
        keys.push_back(Value(key.first));
      }
      return Value(std::move(keys));
    }
    if (object.isBytecodeInstance()) {
      for (const auto& field : object.asBytecodeInstance()->fields) {
        keys.push_back(Value(field.first));
      }
      return Value(std::move(keys));
    }
    if (object.isArray()) {
      keys.push_back(Value(std::string("length")));
      keys.push_back(Value(std::string("push")));
      keys.push_back(Value(std::string("pop")));
      return Value(std::move(keys));
    }
    if (object.isString()) {
      keys.push_back(Value(std::string("length")));
      return Value(std::move(keys));
    }

    return Value(std::move(keys));
  });
}

void VM::defineBuiltin(std::string name, std::size_t arity, NativeFn function) {
  auto native = std::make_shared<NativeFunction>();
  native->name = name;
  native->arity = arity;
  native->function = std::move(function);

  globals_[native->name] = Value(native);
}

Value VM::run(const Chunk& chunk) {
  stack_.clear();
  frames_.clear();
  openUpvalues_.clear();

  auto script = std::make_shared<BytecodeFunction>();
  script->name = "<script>";
  script->chunk = chunk;
  auto scriptClosure = std::make_shared<BytecodeClosure>();
  scriptClosure->function = script;

  frames_.push_back(CallFrame{scriptClosure, 0, 0, 0});

  while (true) {
    CallFrame& frame = frames_.back();
    const Chunk& chunk = frame.closure->function->chunk;

    Opcode opcode = static_cast<Opcode>(chunk.readByte(frame.ip++));

    switch (opcode) {
      case Opcode::Constant: {
        const std::uint8_t index = chunk.readByte(frame.ip++);
        push(chunk.constant(index));
        break;
      }
      case Opcode::Add: {
        Value right = pop();
        Value left = pop();
        if (left.isString() || right.isString()) {
          push(Value(left.toString() + right.toString()));
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
          return result;
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
          auto native = callee.asNativeFunction();
          expectArity(argCount, native->arity, native->name);
            
          std::vector<Value> arguments;
          arguments.reserve(argCount);
          for (std::size_t i = 0; i < argCount; ++i) {
            arguments.push_back(stack_[calleeIndex + 1 + i]);
          }

          Value result = native->function(arguments);
          stack_.resize(calleeIndex);
          push(result);
          break;
        }

        if (callee.isBytecodeClass()) {
          auto klass = callee.asBytecodeClass();
          auto init = findMethod(klass, "init");
          if (init == nullptr && argCount != 0) {
            throw RuntimeError("class " + klass->name + " expects 0 arguments");
          }

          auto instance = std::make_shared<BytecodeInstance>();
          instance->klass = klass;
          if (init == nullptr) {
            stack_.resize(calleeIndex);
            push(Value(instance));
            break;
          }

          stack_[calleeIndex] = Value(instance);
          callBytecodeClosure(init, argCount, calleeIndex, calleeIndex, "method", true);
          break;
        }

        if (callee.isBytecodeBoundMethod()) {
          auto boundMethod = callee.asBytecodeBoundMethod();
          stack_[calleeIndex] = Value(boundMethod->receiver);
          callBytecodeClosure(boundMethod->method, argCount, calleeIndex, calleeIndex, "method");
          break;
        }

        if (callee.isBytecodeFunction() || callee.isBytecodeClosure()) {
          std::shared_ptr<BytecodeClosure> closure;
          if (callee.isBytecodeClosure()) {
            closure = callee.asBytecodeClosure();
          } else {
            closure = std::make_shared<BytecodeClosure>();
            closure->function = callee.asBytecodeFunction();
          }

          // callee 位于参数前一个槽位；新函数帧从第一个参数开始。
          // 因此 OP_GET_LOCAL 0 读取的就是第一个实参，无需复制参数数组。
          callBytecodeClosure(closure, argCount, calleeIndex, calleeIndex + 1, "function");
          break;
        }

        throw RuntimeError("value is not callable");
      }
      case Opcode::Class: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        auto klass = std::make_shared<BytecodeClass>();
        klass->name = chunk.constant(nameIndex).asString();
        push(Value(klass));
        break;
      }
      case Opcode::Method: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();
        Value method = pop();
        Value klass = peek();
        klass.asBytecodeClass()->methods[name] = method.asBytecodeClosure();
        break;
      }
      case Opcode::StaticMethod: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();
        Value method = pop();
        Value klass = peek();
        klass.asBytecodeClass()->staticMethods[name] = method.asBytecodeClosure();
        break;
      }
      case Opcode::Inherit: {
        Value superclass = pop();
        Value subclass = peek();
        if (!superclass.isBytecodeClass()) {
          throw RuntimeError("superclass must be a class");
        }
        if (!subclass.isBytecodeClass()) {
          throw RuntimeError("subclass must be a class");
        }

        subclass.asBytecodeClass()->superclass = superclass.asBytecodeClass();
        break;
      }
      case Opcode::Closure: {
        const std::uint8_t functionIndex = chunk.readByte(frame.ip++);
        auto function = chunk.constant(functionIndex).asBytecodeFunction();
        auto closure = std::make_shared<BytecodeClosure>();
        closure->function = function;
        closure->upvalues.reserve(function->upvalues.size());

        for (const UpvalueDescriptor& descriptor : function->upvalues) {
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

        push(Value(closure));
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
        if (receiver.isBytecodeInstance()) {
          auto instance = receiver.asBytecodeInstance();
          auto method = findMethod(instance->klass, name);
          if (method == nullptr) {
            throw RuntimeError("value has no method: " + name);
          }

          callBytecodeClosure(method, argCount, receiverIndex, receiverIndex, "method");
          break;
        }

        if (receiver.isBytecodeClass()) {
          auto method = findStaticMethod(receiver.asBytecodeClass(), name);
          if (method == nullptr) {
            throw RuntimeError("value has no method: " + name);
          }

          callBytecodeClosure(method, argCount, receiverIndex, receiverIndex + 1, "function");
          break;
        }

        if (!receiver.isArray()) {
          throw RuntimeError("method call receiver is not an array");
        }

        std::vector<Value>& array = receiver.asArray();

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
        if (!superclass.isBytecodeClass()) {
          throw RuntimeError("superclass must be a class");
        }
        if (!receiver.isBytecodeInstance()) {
          throw RuntimeError("this must be an instance");
        }

        auto method = findMethod(superclass.asBytecodeClass(), name);
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
        std::vector<Value> elements(count);
        for (std::size_t i = count; i > 0; --i) {
          elements[i - 1] = pop();
        }
        push(Value(std::move(elements)));
        break;
      }
      case Opcode::GetIndex: {
        Value index = pop();
        Value array = pop();
        const std::vector<Value>& elements = array.asArray();
        push(elements[arrayIndexFromValue(index, elements.size())]);
        break;
      }
      case Opcode::SetIndex: {
        Value value = pop();
        Value index = pop();
        Value array = pop();
        std::vector<Value>& elements = array.asArray();
        elements[arrayIndexFromValue(index, elements.size())] = value;
        push(value);
        break;
      }
      case Opcode::Object: {
        const std::uint8_t namesIndex = chunk.readByte(frame.ip++);
        const std::vector<Value>& names = chunk.constant(namesIndex).asArray();

        std::unordered_map<std::string, Value> properties;
        for (std::size_t i = names.size(); i > 0; --i) {
          Value value = pop();
          const std::string& name = names[i - 1].asString();
          properties[name] = value;
        }
        push(Value(std::move(properties)));
        break;
      }
      case Opcode::GetProperty: {
        const std::uint8_t nameIndex = chunk.readByte(frame.ip++);
        const std::string& name = chunk.constant(nameIndex).asString();

        Value object = pop();

        if (object.isArray()) {
          if (name == "length") {
            push(Value(static_cast<double>(object.asArray().size())));
            break;
          }

          push(Value::undefined());
          break;
        }

        if (object.isString()) {
          if (name == "length") {
            push(Value(static_cast<double>(object.asString().size())));
            break;
          }

          push(Value::undefined());
          break;
        }

        if (object.isBytecodeInstance()) {
          auto instance = object.asBytecodeInstance();
          auto field = instance->fields.find(name);
          if (field != instance->fields.end()) {
            push(field->second);
            break;
          }

          auto method = findMethod(instance->klass, name);
          if (method != nullptr) {
            auto boundMethod = std::make_shared<BytecodeBoundMethod>();
            boundMethod->receiver = instance;
            boundMethod->method = method;
            push(Value(boundMethod));
            break;
          }

          push(Value::undefined());
          break;
        }

        if (object.isBytecodeClass()) {
          auto method = findStaticMethod(object.asBytecodeClass(), name);
          if (method != nullptr) {
            push(Value(method));
            break;
          }

          push(Value::undefined());
          break;
        }

        const auto& properties = object.asObject();

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
        if (object.isBytecodeInstance()) {
          object.asBytecodeInstance()->fields[name] = value;
          push(value);
          break;
        }

        object.asObject()[name] = value;
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

void VM::callBytecodeClosure(std::shared_ptr<BytecodeClosure> closure, std::size_t argCount,
                             std::size_t returnSlot, std::size_t slotStart,
                             const std::string& label, bool returnsReceiver) {
  auto function = closure->function;
  if (argCount != function->params.size()) {
    throw RuntimeError(label + " " + function->name + " expects " +
                       std::to_string(function->params.size()) + " arguments");
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
std::shared_ptr<Upvalue> VM::captureUpvalue(std::size_t stackIndex) {
  for (const auto& upvalue : openUpvalues_) {
    if (!upvalue->isClosed && upvalue->stackIndex == stackIndex) {
      return upvalue;
    }
  }

  auto upvalue = std::make_shared<Upvalue>();
  upvalue->stackIndex = stackIndex;
  openUpvalues_.push_back(upvalue);
  return upvalue;
}

// 关闭所有指向即将离开栈的 upvalue。
// 值从 stack_ 复制到 Upvalue::closed，之后闭包从 closed 读写。
void VM::closeUpvalues(std::size_t firstStackIndex) {
  for (const auto& upvalue : openUpvalues_) {
    if (!upvalue->isClosed && upvalue->stackIndex >= firstStackIndex) {
      upvalue->closed = stack_[upvalue->stackIndex];
      upvalue->isClosed = true;
    }
  }
  openUpvalues_.erase(
      std::remove_if(openUpvalues_.begin(), openUpvalues_.end(),
                     [](const std::shared_ptr<Upvalue>& upvalue) { return upvalue->isClosed; }),
      openUpvalues_.end());
}

}  // namespace minijs
