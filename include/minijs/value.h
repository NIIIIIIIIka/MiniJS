#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace minijs {

struct BytecodeFunction;
struct BytecodeClosure;
struct BytecodeClass;
struct BytecodeInstance;
struct BytecodeBoundMethod;
struct ClassValue;
struct InstanceValue;
struct BoundMethodValue;
class Environment;
class FunctionStmt;
class Value;

using BuiltinFunction = std::function<Value(const std::vector<Value>& arguments)>;
using NativeFn = std::function<Value(const std::vector<Value>& arguments)>;

// C++ 实现的原生函数载荷。builtin 是注册到全局环境的名字，native 是值的实现形态。
struct NativeFunction {
  std::string name;
  std::size_t arity = 0;
  NativeFn function;
};

// AST 解释器使用的函数运行时载荷。
struct FunctionValue {
  const FunctionStmt* declaration = nullptr;
  std::shared_ptr<Environment> closure;
};

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
  NativeFunction,
  BytecodeFunction,
  BytecodeClosure,
  BytecodeClass,
  BytecodeInstance,
  BytecodeBoundMethod,
  InterpreterClass,
  InterpreterInstance,
  InterpreterBoundMethod,
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

  // 创建 AST 解释器函数值。
  Value(const FunctionStmt* declaration, std::shared_ptr<Environment> closure);

  // 创建 C++ 实现的内置函数值。
  explicit Value(BuiltinFunction builtin);

  // 创建 C++ 实现的原生函数值。
  explicit Value(std::shared_ptr<NativeFunction> function);

  // 创建数组值，数组使用共享指针模拟对象引用语义。
  explicit Value(std::vector<Value> elements);

  // 创建对象值，对象使用共享指针模拟对象引用语义。
  explicit Value(std::unordered_map<std::string, Value> properties);

  // 创建字符串值。
  explicit Value(std::string string);

  // 创建字节码函数值。
  explicit Value(std::shared_ptr<BytecodeFunction> function);

  // 创建字节码闭包值。
  explicit Value(std::shared_ptr<BytecodeClosure> closure);

  // 创建字节码类值。
  explicit Value(std::shared_ptr<BytecodeClass> klass);

  // 创建字节码实例值。
  explicit Value(std::shared_ptr<BytecodeInstance> instance);

  // 创建字节码绑定方法值。
  explicit Value(std::shared_ptr<BytecodeBoundMethod> method);

  // 创建 AST 解释器类值。
  explicit Value(std::shared_ptr<ClassValue> klass);

  // 创建 AST 解释器实例值。
  explicit Value(std::shared_ptr<InstanceValue> instance);

  // 创建 AST 解释器绑定方法值。
  explicit Value(std::shared_ptr<BoundMethodValue> method);

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

  // 返回内置函数载荷；当前值不是内置函数时抛出运行时错误。
  const BuiltinFunction& asBuiltinFunction() const;

  // 返回原生函数载荷；当前值不是原生函数时抛出运行时错误。
  const std::shared_ptr<NativeFunction>& asNativeFunction() const;

  // 返回字节码函数载荷；当前值不是字节码函数时抛出运行时错误。
  const std::shared_ptr<BytecodeFunction>& asBytecodeFunction() const;

  // 返回字节码闭包载荷；当前值不是字节码闭包时抛出运行时错误。
  const std::shared_ptr<BytecodeClosure>& asBytecodeClosure() const;

  // 返回字节码类载荷；当前值不是字节码类时抛出运行时错误。
  const std::shared_ptr<BytecodeClass>& asBytecodeClass() const;

  // 返回字节码实例载荷；当前值不是字节码实例时抛出运行时错误。
  const std::shared_ptr<BytecodeInstance>& asBytecodeInstance() const;

  // 返回字节码绑定方法载荷；当前值不是字节码绑定方法时抛出运行时错误。
  const std::shared_ptr<BytecodeBoundMethod>& asBytecodeBoundMethod() const;

  // 返回 AST 解释器类载荷；当前值不是类时抛出运行时错误。
  const std::shared_ptr<ClassValue>& asClass() const;

  // 返回 AST 解释器实例载荷；当前值不是实例时抛出运行时错误。
  const std::shared_ptr<InstanceValue>& asInstance() const;

  // 返回 AST 解释器绑定方法载荷；当前值不是绑定方法时抛出运行时错误。
  const std::shared_ptr<BoundMethodValue>& asBoundMethod() const;

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

  // 返回当前值是否为布尔值。
  bool isBoolean() const;

  // 返回当前值是否为字符串。
  bool isString() const;

  // 返回当前值是否为函数。
  bool isFunction() const;

  // 返回当前值是否为数组。
  bool isArray() const;

  // 返回当前值是否为对象。
  bool isObject() const;

  // 返回当前值是否为内置函数。
  bool isBuiltinFunction() const;

  // 返回当前值是否为原生函数。
  bool isNativeFunction() const;

  // 返回当前值是否为字节码函数。
  bool isBytecodeFunction() const;

  // 返回当前值是否为字节码闭包。
  bool isBytecodeClosure() const;

  // 返回当前值是否为字节码类。
  bool isBytecodeClass() const;

  // 返回当前值是否为字节码实例。
  bool isBytecodeInstance() const;

  // 返回当前值是否为字节码绑定方法。
  bool isBytecodeBoundMethod() const;

  // 返回当前值是否为 AST 解释器类。
  bool isClass() const;

  // 返回当前值是否为 AST 解释器实例。
  bool isInstance() const;

  // 返回当前值是否为 AST 解释器绑定方法。
  bool isBoundMethod() const;

  // 比较两个运行时值是否相等；对象、数组和函数按引用身份比较。
  bool equals(const Value& other) const;

 private:
  explicit Value(ValueType type);

  ValueType value_type_ = ValueType::Null;
  double number_ = 0;
  bool boolean_ = false;
  FunctionValue function_ = FunctionValue({nullptr, nullptr});
  std::shared_ptr<BytecodeFunction> bytecode_function_;
  std::shared_ptr<BytecodeClosure> bytecode_closure_;
  std::shared_ptr<BytecodeClass> bytecode_class_;
  std::shared_ptr<BytecodeInstance> bytecode_instance_;
  std::shared_ptr<BytecodeBoundMethod> bytecode_bound_method_;
  std::shared_ptr<ClassValue> class_;
  std::shared_ptr<InstanceValue> instance_;
  std::shared_ptr<BoundMethodValue> bound_method_;

  std::shared_ptr<NativeFunction> native_function_;
  std::shared_ptr<std::vector<Value>> array_;
  std::shared_ptr<std::unordered_map<std::string, Value>> object_;
  std::string string_;
};

// 字节码 VM 使用的类运行时载荷，保存类名和方法表。
struct BytecodeClass {
  std::string name;
  std::unordered_map<std::string, std::shared_ptr<BytecodeClosure>> methods;
};

// 字节码 VM 使用的实例运行时载荷，字段保存在实例自身。
struct BytecodeInstance {
  std::shared_ptr<BytecodeClass> klass;
  std::unordered_map<std::string, Value> fields;
};

// 字节码 VM 使用的绑定方法载荷，保存 receiver 和方法闭包。
struct BytecodeBoundMethod {
  std::shared_ptr<BytecodeInstance> receiver;
  std::shared_ptr<BytecodeClosure> method;
};

// AST 解释器使用的类运行时载荷，保存类名和方法表。
struct ClassValue {
  std::string name;
  std::unordered_map<std::string, FunctionValue> methods;
};

// AST 解释器使用的实例运行时载荷，字段保存在实例自身。
struct InstanceValue {
  std::shared_ptr<ClassValue> klass;
  std::unordered_map<std::string, Value> fields;
};

// AST 解释器使用的绑定方法载荷，保存接收者实例和原始方法函数。
struct BoundMethodValue {
  std::shared_ptr<InstanceValue> receiver;
  FunctionValue method;
};

}  // namespace minijs
