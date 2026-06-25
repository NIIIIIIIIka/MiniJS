#pragma once

#include <string>

namespace minijs {

class Value {
 public:
  Value();
  explicit Value(double number);

  double asNumber() const;
  std::string toString() const;

 private:
  double number_ = 0;
};

}  // namespace minijs
