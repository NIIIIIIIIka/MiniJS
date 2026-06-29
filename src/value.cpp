#include "minijs/value.h"

#include <cmath>
#include <sstream>
#include <utility>

#include "minijs/runtime_error.h"

namespace minijs {

Value::Value() = default;

Value::Value(double number) : value_type_(ValueType::Number), number_(number) {}

Value::Value(bool boolean) : value_type_(ValueType::Boolean), boolean_(boolean) {}

Value::Value(const FunctionStmt* declaration, Environment* closure)
    : value_type_(ValueType::Function), function_({declaration, closure}) {}

Value::Value(std::vector<Value> elements)
    : value_type_(ValueType::Array),
      array_(std::make_shared<std::vector<Value>>(std::move(elements))) {}

double Value::asNumber() const {
  if (!isNumber()) {
    throw RuntimeError("value is not a number");
  }
  return number_;
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
    case ValueType::Null:
      return "null";
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
    case ValueType::Array:
      return true;
    case ValueType::Null:
      return false;
  }

  return false;
}

bool Value::isNull() const { return value_type_ == ValueType::Null; }

bool Value::isNumber() const { return value_type_ == ValueType::Number; }

bool Value::isFunction() const { return value_type_ == ValueType::Function; }

bool Value::isArray() const { return value_type_ == ValueType::Array; }

}  // namespace minijs
