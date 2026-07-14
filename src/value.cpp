#include "minijs/value.h"

#include <cmath>
#include <sstream>
#include <utility>

#include "minijs/ast.h"
#include "minijs/bytecode_closure.h"
#include "minijs/bytecode_function.h"
#include "minijs/object.h"
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

Value::Value(ObjString* string) : value_type_(ValueType::GcString), gc_string_(string) {}

Value::Value(std::shared_ptr<BytecodeFunction> function)
    : value_type_(ValueType::BytecodeFunction), bytecode_function_(std::move(function)) {}

Value::Value(std::shared_ptr<BytecodeClosure> closure)
    : value_type_(ValueType::BytecodeClosure), bytecode_closure_(std::move(closure)) {}

Value::Value(std::shared_ptr<BytecodeClass> klass)
    : value_type_(ValueType::BytecodeClass), bytecode_class_(std::move(klass)) {}

Value::Value(std::shared_ptr<BytecodeInstance> instance)
    : value_type_(ValueType::BytecodeInstance), bytecode_instance_(std::move(instance)) {}

Value::Value(std::shared_ptr<BytecodeBoundMethod> method)
    : value_type_(ValueType::BytecodeBoundMethod), bytecode_bound_method_(std::move(method)) {}

Value::Value(std::shared_ptr<ClassValue> klass)
    : value_type_(ValueType::InterpreterClass), class_(std::move(klass)) {}

Value::Value(std::shared_ptr<InstanceValue> instance)
    : value_type_(ValueType::InterpreterInstance), instance_(std::move(instance)) {}

Value::Value(std::shared_ptr<BoundMethodValue> method)
    : value_type_(ValueType::InterpreterBoundMethod), bound_method_(std::move(method)) {}

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
  if (value_type_ == ValueType::GcString) {
    return gc_string_->value;
  }
  return string_;
}

const FunctionValue& Value::asFunction() const {
  if (!isFunction()) {
    throw RuntimeError("value is not a function");
  }
  return function_;
}

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

const std::shared_ptr<BytecodeClosure>& Value::asBytecodeClosure() const {
  if (!isBytecodeClosure()) {
    throw RuntimeError("value is not a bytecode closure");
  }
  return bytecode_closure_;
}

const std::shared_ptr<BytecodeClass>& Value::asBytecodeClass() const {
  if (!isBytecodeClass()) {
    throw RuntimeError("value is not a bytecode class");
  }
  return bytecode_class_;
}

const std::shared_ptr<BytecodeInstance>& Value::asBytecodeInstance() const {
  if (!isBytecodeInstance()) {
    throw RuntimeError("value is not a bytecode instance");
  }
  return bytecode_instance_;
}

const std::shared_ptr<BytecodeBoundMethod>& Value::asBytecodeBoundMethod() const {
  if (!isBytecodeBoundMethod()) {
    throw RuntimeError("value is not a bytecode bound method");
  }
  return bytecode_bound_method_;
}

const std::shared_ptr<ClassValue>& Value::asClass() const {
  if (!isClass()) {
    throw RuntimeError("value is not a class");
  }
  return class_;
}

const std::shared_ptr<InstanceValue>& Value::asInstance() const {
  if (!isInstance()) {
    throw RuntimeError("value is not an instance");
  }
  return instance_;
}

const std::shared_ptr<BoundMethodValue>& Value::asBoundMethod() const {
  if (!isBoundMethod()) {
    throw RuntimeError("value is not a bound method");
  }
  return bound_method_;
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
    case ValueType::BytecodeClosure:
      return "<function " + bytecode_closure_->function->name + ">";
    case ValueType::BytecodeClass:
      return "<class " + bytecode_class_->name + ">";
    case ValueType::BytecodeInstance:
      return "<" + bytecode_instance_->klass->name + " instance>";
    case ValueType::BytecodeBoundMethod:
      return "<bound method " + bytecode_bound_method_->method->function->name + ">";
    case ValueType::InterpreterClass:
      return "<class " + class_->name + ">";
    case ValueType::InterpreterInstance:
      return "<" + instance_->klass->name + " instance>";
    case ValueType::InterpreterBoundMethod:
      if (bound_method_->method.declaration == nullptr) {
        return "<bound method>";
      }
      return "<bound method " + bound_method_->method.declaration->name() + ">";
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
    case ValueType::GcString:
      return gc_string_->value;
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
    case ValueType::BytecodeClosure:
    case ValueType::BytecodeClass:
    case ValueType::BytecodeInstance:
    case ValueType::BytecodeBoundMethod:
    case ValueType::InterpreterClass:
    case ValueType::InterpreterInstance:
    case ValueType::InterpreterBoundMethod:
      return true;
    case ValueType::Null:
      return false;
    case ValueType::String:
      return !string_.empty();
    case ValueType::GcString:
      return !gc_string_->value.empty();
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

bool Value::isString() const {
  return value_type_ == ValueType::String || value_type_ == ValueType::GcString;
}

bool Value::isObject() const { return value_type_ == ValueType::Object; }

bool Value::isBuiltinFunction() const { return isNativeFunction(); }

bool Value::isNativeFunction() const { return value_type_ == ValueType::NativeFunction; }

bool Value::isBytecodeFunction() const { return value_type_ == ValueType::BytecodeFunction; }

bool Value::isBytecodeClosure() const { return value_type_ == ValueType::BytecodeClosure; }

bool Value::isBytecodeClass() const { return value_type_ == ValueType::BytecodeClass; }

bool Value::isBytecodeInstance() const { return value_type_ == ValueType::BytecodeInstance; }

bool Value::isBytecodeBoundMethod() const {
  return value_type_ == ValueType::BytecodeBoundMethod;
}

bool Value::isClass() const { return value_type_ == ValueType::InterpreterClass; }

bool Value::isInstance() const { return value_type_ == ValueType::InterpreterInstance; }

bool Value::isBoundMethod() const { return value_type_ == ValueType::InterpreterBoundMethod; }

bool Value::equals(const Value& other) const {
  if (isString() && other.isString()) {
    return asString() == other.asString();
  }

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
    case ValueType::GcString:
      return gc_string_ == other.gc_string_;
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
    case ValueType::BytecodeClosure:
      return bytecode_closure_ == other.bytecode_closure_;
    case ValueType::BytecodeClass:
      return bytecode_class_ == other.bytecode_class_;
    case ValueType::BytecodeInstance:
      return bytecode_instance_ == other.bytecode_instance_;
    case ValueType::BytecodeBoundMethod:
      return bytecode_bound_method_ == other.bytecode_bound_method_;
    case ValueType::InterpreterClass:
      return class_ == other.class_;
    case ValueType::InterpreterInstance:
      return instance_ == other.instance_;
    case ValueType::InterpreterBoundMethod:
      return bound_method_ == other.bound_method_;
    case ValueType::NativeFunction:
      return native_function_ == other.native_function_;
  }

  return false;
}

}  // namespace minijs
