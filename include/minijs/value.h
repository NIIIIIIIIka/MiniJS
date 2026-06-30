#pragma once

#include <memory>
#include <string>
#include <unordered_map>
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
  Object,
  String,
  Undefined,
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

  // 创建对象值，对象使用共享指针模拟对象引用语义。
  explicit Value(std::unordered_map<std::string, Value> properties);

  // 创建字符串值。
  explicit Value(std::string string);

  // 创建 undefined 值。
  static Value undefined();

  // 返回数字载荷；当前值不是数字时抛出运行时错误。
  double asNumber() const;

  // 返回字符串载荷；当前值不是字符串时抛出运行时错误。
  const std::string& asString() const;

  // 返回函数载荷；调用方应保证当前值是函数。
  const FunctionValue& asFunction() const;

  // 返回数组载荷；当前值不是数组时抛出运行时错误。
  const std::vector<Value>& asArray() const;

  // 返回可修改数组载荷；当前值不是数组时抛出运行时错误。
  std::vector<Value>& asArray();

  // 返回对象载荷；当前值不是对象时抛出运行时错误。
  const std::unordered_map<std::string, Value>& asObject() const;

  // 返回可修改对象载荷；当前值不是对象时抛出运行时错误。
  std::unordered_map<std::string, Value>& asObject();

  // 返回面向用户的字符串表示。
  std::string toString() const;

  // 返回该值在条件表达式中的真假性。
  bool isTruthy() const;

  // 返回当前值是否为 null。
  bool isNull() const;

  // 返回当前值是否为 undefined。
  bool isUndefined() const;

  // 返回当前值是否为数字。
  bool isNumber() const;

  // 返回当前值是否为字符串。
  bool isString() const;

  // 返回当前值是否为函数。
  bool isFunction() const;

  // 返回当前值是否为数组。
  bool isArray() const;

  // 返回当前值是否为对象。
  bool isObject() const;

 private:
  explicit Value(ValueType type);

  ValueType value_type_ = ValueType::Null;
  double number_ = 0;
  bool boolean_ = false;
  FunctionValue function_ = FunctionValue({nullptr, nullptr});
  std::shared_ptr<std::vector<Value>> array_;
  std::shared_ptr<std::unordered_map<std::string, Value>> object_;
  std::string string_;
};

}  // namespace minijs
