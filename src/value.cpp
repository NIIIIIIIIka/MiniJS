#include "minijs/value.h"

#include <cmath>
#include <sstream>

namespace minijs {

Value::Value() = default;

Value::Value(double number) : value_type_(ValueType::Number), number_(number) {}

Value::Value(bool boolean) : value_type_(ValueType::Boolean), boolean_(boolean) {}

double Value::asNumber() const { return number_; }

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
    case ValueType::Null:
      return false;
  }

  return false;
}

}  // namespace minijs
