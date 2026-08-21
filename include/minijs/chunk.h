#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "minijs/value.h"

namespace minijs {

struct ObjShape;

struct PropertyInlineCache {
  bool initialized = false;
  ObjShape* shape = nullptr;
  std::size_t slot = 0;
};

// VM 支持的字节码指令。
enum class Opcode : std::uint8_t {
  Constant,
  Add,
  Sub,
  Mul,
  Div,
  Mod,
  Negate,
  Return,
  DefineGlobal,
  GetGlobal,
  SetGlobal,
  GetLocal,
  SetLocal,
  Pop,
  Equal,
  Greater,
  Less,
  Not,
  JumpIfFalse,
  Jump,
  Loop,
  Call,
  Array,
  GetIndex,
  SetIndex,
  Object,
  GetProperty,
  SetProperty,
  MethodCall,
  Closure,
  GetUpvalue,
  SetUpvalue,
  CloseUpvalue,
  GetCurrentClosure,
  Class,
  Method,
  StaticMethod,
  Inherit,
  SuperCall,
};

// 一段可执行字节码，包含指令流和常量池。
class Chunk {
 public:
  Chunk() = default;
  Chunk(const Chunk& other);
  Chunk& operator=(const Chunk& other);
  Chunk(Chunk&& other) noexcept;
  Chunk& operator=(Chunk&& other) noexcept;

  void writeOpcode(Opcode opcode);
  void writeByte(std::uint8_t byte);

  std::uint8_t readByte(std::size_t offset) const;

  std::size_t addConstant(Value value);
  const Value& constant(std::size_t index) const;

  const std::vector<std::uint8_t>& code() const;
  const std::vector<Value>& constants() const;

  // 当前指令流长度，用于编译跳转目标。
  std::size_t count() const;
  // 回填已写入的占位字节，例如跳转偏移。
  void patchByte(std::size_t offset, std::uint8_t byte);

  PropertyInlineCache& propertyInlineCache(std::size_t opcodeOffset) const;
  void clearInlineCaches() const;

 private:
  std::vector<std::uint8_t> code_;
  std::vector<Value> constants_;
  mutable std::unordered_map<std::size_t, PropertyInlineCache> propertyInlineCaches_;
};

}  // namespace minijs
