#include "minijs/vm.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
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

}  // namespace

Value VM::run(const Chunk& chunk) {
  stack_.clear();
  std::size_t ip = 0;

  while (true) {
    const Opcode opcode = static_cast<Opcode>(chunk.readByte(ip++));

    switch (opcode) {
      case Opcode::Constant: {
        const std::uint8_t index = chunk.readByte(ip++);
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
      case Opcode::Return:
        return pop();

      case Opcode::DefineGlobal: {
        const std::string name = chunk.constant(chunk.readByte(ip++)).asString();
        globals_[name] = pop();
        break;
      }
      case Opcode::GetGlobal: {
        const std::string name = chunk.constant(chunk.readByte(ip++)).asString();
        auto it = globals_.find(name);
        if (it == globals_.end()) {
          throw RuntimeError("undefined variable: " + name);
        }
        push(it->second);
        break;
      }
      case Opcode::SetGlobal: {
        const std::string name = chunk.constant(chunk.readByte(ip++)).asString();
        auto it = globals_.find(name);
        if (it == globals_.end()) {
          throw RuntimeError("undefined variable: " + name);
        }
        it->second = peek();
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
        const std::uint16_t offset = readShort(chunk, ip);
        if (!peek().isTruthy()) {
          ip += offset;
        }
        break;
      }
      case Opcode::Jump: {
        const std::uint16_t offset = readShort(chunk, ip);
        ip += offset;
        break;
      }
      case Opcode::Loop: {
        const std::uint16_t offset = readShort(chunk, ip);
        ip -= offset;
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
