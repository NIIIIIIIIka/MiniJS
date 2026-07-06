#include "minijs/vm.h"

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

}  // namespace

VM::VM() {
  defineBuiltin("print", 1, [](const std::vector<Value>& arguments) -> Value {
    std::cout << arguments[0].toString() << '\n';
    return Value();
  });

  defineBuiltin("clock", 0, [](const std::vector<Value>&) -> Value {
    return Value(0.0);
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

  auto script = std::make_shared<BytecodeFunction>();
  script->name = "<script>";
  script->chunk = chunk;

  frames_.push_back(CallFrame{script, 0, 0});

  while (true) {
    CallFrame& frame = frames_.back();
    const Chunk& chunk = frame.function->chunk;

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

        CallFrame frame = frames_.back();
        frames_.pop_back();

        const std::size_t calleeIndex = frame.slotStart - 1;
        stack_.resize(calleeIndex);
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

        if (callee.isBytecodeFunction()) {
          auto function = callee.asBytecodeFunction();

          if (argCount != function->params.size()) {
            throw RuntimeError("function " + function->name + " expects " +
                               std::to_string(function->params.size()) + " arguments");
          }

          frames_.push_back(CallFrame{
              function,
              0,
              calleeIndex + 1,
          });
          break;
        }

        throw RuntimeError("value is not callable");
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

}  // namespace minijs
