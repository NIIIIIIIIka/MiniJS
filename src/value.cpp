#include "minijs/value.h"

#include <cmath>
#include <sstream>
#include <utility>

#include "minijs/bytecode_function.h"
#include "minijs/runtime_error.h"

namespace minijs {

Value::Value() = default;

Value::Value(ValueType type) : value_type_(type) {}

Value::Value(double number) : value_type_(ValueType::Number), number_(number) {}

Value::Value(bool boolean) : value_type_(ValueType::Boolean), boolean_(boolean) {}

Value::Value(const FunctionStmt* declaration, std::shared_ptr<Environment> closure)
    : value_type_(ValueType::Function), function_({declaration, closure}) {}

Value::Value(BuiltinFunction builtin)
    : value_type_(ValueType::NativeFunction),
      native_function_(
          std::make_shared<NativeFunction>(NativeFunction{"<builtin>", 0, std::move(builtin)})) {}

Value::Value(std::shared_ptr<NativeFunction> function)
    : value_type_(ValueType::NativeFunction), native_function_(std::move(function)) {}

Value::Value(std::vector<Value> elements)
    : value_type_(ValueType::Array),
      array_(std::make_shared<std::vector<Value>>(std::move(elements))) {}

Value::Value(std::unordered_map<std::string, Value> properties)
    : value_type_(ValueType::Object),
      object_(std::make_shared<std::unordered_map<std::string, Value>>(std::move(properties))) {}

Value::Value(std::string string) : value_type_(ValueType::String), string_(std::move(string)) {}

Value::Value(std::shared_ptr<BytecodeFunction> function)
    : value_type_(ValueType::BytecodeFunction), bytecode_function_(std::move(function)) {}

Value Value::undefined() { return Value(ValueType::Undefined); }

double Value::asNumber() const {
  if (!isNumber()) {
    throw RuntimeError("value is not a number");
  }
  return number_;
}

const std::string& Value::asString() const {
  if (!isString()) {
    throw RuntimeError("value is not a string");
  }
  return string_;
}

const FunctionValue& Value::asFunction() const { return function_; }

const std::vector<Value>& Value::asArray() const {
  if (!isArray()) {
    throw RuntimeError("value is not an array");
  }
  return *array_;
}

std::vector<Value>& Value::asArray() {
  if (!isArray()) {
    throw RuntimeError("value is not an array");
  }
  return *array_;
}

const std::unordered_map<std::string, Value>& Value::asObject() const {
  if (!isObject()) {
    throw RuntimeError("value is not an object");
  }
  return *object_;
}

std::unordered_map<std::string, Value>& Value::asObject() {
  if (!isObject()) {
    throw RuntimeError("value is not an object");
  }
  return *object_;
}

const BuiltinFunction& Value::asBuiltinFunction() const { return asNativeFunction()->function; }

const std::shared_ptr<NativeFunction>& Value::asNativeFunction() const {
  if (!isNativeFunction()) {
    throw RuntimeError("value is not a native function");
  }
  return native_function_;
}

const std::shared_ptr<BytecodeFunction>& Value::asBytecodeFunction() const {
  if (!isBytecodeFunction()) {
    throw RuntimeError("value is not a bytecode function");
  }
  return bytecode_function_;
}

std::string Value::toString() const {
  switch (value_type_) {
    case ValueType::Number: {
      if (std::trunc(number_) == number_) {
        return std::to_string(static_cast<long long>(number_));
      }

      std::ostringstream output;
      output << number_;
      return output.str();
    }
    case ValueType::Boolean:
      return boolean_ ? "true" : "false";
    case ValueType::Function:
      return "<function>";
    case ValueType::NativeFunction:
      return "<native function " + native_function_->name + ">";
    case ValueType::BytecodeFunction:
      return "<function " + bytecode_function_->name + ">";
    case ValueType::Array: {
      std::string result = "[";
      for (std::size_t i = 0; i < array_->size(); ++i) {
        if (i != 0) {
          result += ", ";
        }
        result += (*array_)[i].toString();
      }
      result += "]";
      return result;
    }
    case ValueType::Object:
      return "[object Object]";
    case ValueType::String:
      return string_;
    case ValueType::Null:
      return "null";
    case ValueType::Undefined:
      return "undefined";
  }

  return "null";
}

bool Value::isTruthy() const {
  switch (value_type_) {
    case ValueType::Number:
      return number_ != 0;
    case ValueType::Boolean:
      return boolean_;
    case ValueType::Function:
    case ValueType::NativeFunction:
    case ValueType::Array:
    case ValueType::Object:
    case ValueType::BytecodeFunction:
      return true;
    case ValueType::Null:
      return false;
    case ValueType::String:
      return !string_.empty();
    case ValueType::Undefined:
      return false;
  }

  return false;
}

bool Value::isNull() const { return value_type_ == ValueType::Null; }

bool Value::isUndefined() const { return value_type_ == ValueType::Undefined; }

bool Value::isNumber() const { return value_type_ == ValueType::Number; }

bool Value::isBoolean() const { return value_type_ == ValueType::Boolean; }

bool Value::isFunction() const { return value_type_ == ValueType::Function; }

bool Value::isArray() const { return value_type_ == ValueType::Array; }

bool Value::isString() const { return value_type_ == ValueType::String; }

bool Value::isObject() const { return value_type_ == ValueType::Object; }

bool Value::isBuiltinFunction() const { return isNativeFunction(); }

bool Value::isNativeFunction() const { return value_type_ == ValueType::NativeFunction; }

bool Value::isBytecodeFunction() const { return value_type_ == ValueType::BytecodeFunction; }

bool Value::equals(const Value& other) const {
  if (value_type_ != other.value_type_) {
    return false;
  }

  switch (value_type_) {
    case ValueType::Number:
      return number_ == other.number_;
    case ValueType::Boolean:
      return boolean_ == other.boolean_;
    case ValueType::String:
      return string_ == other.string_;
    case ValueType::Null:
    case ValueType::Undefined:
      return true;
    case ValueType::Array:
      return array_ == other.array_;
    case ValueType::Object:
      return object_ == other.object_;
    case ValueType::Function:
      return function_.declaration == other.function_.declaration &&
             function_.closure == other.function_.closure;
    case ValueType::BytecodeFunction:
      return bytecode_function_ == other.bytecode_function_;
    case ValueType::NativeFunction:
      return native_function_ == other.native_function_;
  }

  return false;
}

}  // namespace minijs
