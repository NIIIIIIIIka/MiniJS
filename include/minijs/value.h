#pragma once

#include <memory>
#include <string>
#include <vector>

namespace minijs {

class Environment;
class FunctionStmt;

// 解释器当前支持的运行时值类型。
enum class ValueType {
  Number,
  Boolean,
  Null,
  Function,
  Array,
};

// 函数运行时载荷。
//
// 函数声明节点由 AST 持有；闭包环境由解释器的环境链持有。
// 这里保存非拥有指针，避免在 Value 中复制 AST 或 Environment。
struct FunctionValue {
  const FunctionStmt* declaration = nullptr;
  Environment* closure = nullptr;
};

// MiniJS 的动态运行时值。
class Value {
 public:
  // 创建 null 值。
  Value();

  // 创建数字值。
  explicit Value(double number);

  // 创建布尔值。
  explicit Value(bool boolean);

  // 创建函数值。
  Value(const FunctionStmt* declaration, Environment* closure);

  // 创建数组值，数组使用共享指针模拟对象引用语义。
  explicit Value(std::vector<Value> elements);

  // 返回数字载荷；当前值不是数字时抛出运行时错误。
  double asNumber() const;

  // 返回函数载荷；调用方应保证当前值是函数。
  const FunctionValue& asFunction() const;

  // 返回数组载荷；当前值不是数组时抛出运行时错误。
  const std::vector<Value>& asArray() const;

  // 返回可修改数组载荷；当前值不是数组时抛出运行时错误。
  std::vector<Value>& asArray();

  // 返回面向用户的字符串表示。
  std::string toString() const;

  // 返回该值在条件表达式中的真假性。
  bool isTruthy() const;

  // 返回当前值是否为 null。
  bool isNull() const;

  // 返回当前值是否为数字。
  bool isNumber() const;

  // 返回当前值是否为函数。
  bool isFunction() const;

  // 返回当前值是否为数组。
  bool isArray() const;

 private:
  ValueType value_type_ = ValueType::Null;
  double number_ = 0;
  bool boolean_ = false;
  FunctionValue function_ = FunctionValue({nullptr, nullptr});
  std::shared_ptr<std::vector<Value>> array_;
};

}  // namespace minijs
