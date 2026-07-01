#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "minijs/value.h"

namespace minijs {

// 词法环境，负责保存变量名到运行时值的映射。
//
// 环境可以形成父子链：读取和赋值会沿父链查找，声明只写入当前环境。
class Environment {
 public:
  // 创建没有父环境的根环境。
  Environment();

  // 创建子环境；当前环境找不到变量时会继续查询 parent。
  explicit Environment(std::shared_ptr<Environment> parent);

  // 在当前环境中定义或覆盖一个绑定。
  void define(std::string name, Value value);

  // 在当前环境或父环境中查找绑定。
  Value get(const std::string& name) const;

  // 更新当前环境或父环境中的已有绑定。
  void assign(const std::string& name, Value value);

 private:
  std::shared_ptr<Environment> parent_;
  std::unordered_map<std::string, Value> values_;
};

}  // namespace minijs
