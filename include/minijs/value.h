#pragma once

#include <string>

namespace minijs {

enum class ValueType {
  Number,
  Boolean,
  Null,
};

class Value {
 public:
  Value();
  explicit Value(double number);
  explicit Value(bool boolean);

  double asNumber() const;
  std::string toString() const;
  bool isTruthy() const;

 private:
  ValueType value_type_ = ValueType::Null;
  double number_ = 0;
  bool boolean_ = false;
};

}  // namespace minijs
