#include "minijs/value.h"

#include <cmath>
#include <sstream>

namespace minijs {

Value::Value() = default;

Value::Value(double number) : number_(number) {}

double Value::asNumber() const { return number_; }

std::string Value::toString() const {
  if (std::trunc(number_) == number_) {
    return std::to_string(static_cast<long long>(number_));
  }

  std::ostringstream output;
  output << number_;
  return output.str();
}

}  // namespace minijs
