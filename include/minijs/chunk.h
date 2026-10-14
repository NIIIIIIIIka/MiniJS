#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>
#include "minijs/value.h"

namespace minijs {

struct ObjShape;
struct ObjClass;
struct ObjClosure;

struct PropertyInlineCacheEntry {
  ObjShape* shape = nullptr;
  std::size_t slot = 0;
};

struct PropertyInlineCache {
  static constexpr std::size_t MaxEntries = 4;
  std::array<PropertyInlineCacheEntry, MaxEntries> entries{};
  std::size_t size = 0;
};

struct MethodInlineCacheEntry {
  ObjClass* klass = nullptr;
  ObjClosure* method = nullptr;
  bool isStatic = false;
};

struct MethodInlineCache {
  static constexpr std::size_t MaxEntries = 4;
  std::array<MethodInlineCacheEntry, MaxEntries> entries{};
  std::size_t size = 0;
};

enum class FeedbackKind : std::uint8_t {
  GetProperty,
  SetProperty,
  MethodCall,
};

enum class FeedbackState : std::uint8_t {
  Uninitialized,
  Monomorphic,
  Polymorphic,
  Megamorphic,
};

struct FeedbackSlot {
  FeedbackKind kind;
  FeedbackState state = FeedbackState::Uninitialized;
  PropertyInlineCache property;
  MethodInlineCache method;
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
  std::size_t addFeedbackSlot(FeedbackKind kind);
  FeedbackSlot& feedbackSlot(std::size_t index) const;
  const std::vector<FeedbackSlot>& feedbackSlots() const;

  const std::vector<std::uint8_t>& code() const;
  const std::vector<Value>& constants() const;

  // 当前指令流长度，用于编译跳转目标。
  std::size_t count() const;
  // 回填已写入的占位字节，例如跳转偏移。
  void patchByte(std::size_t offset, std::uint8_t byte);

  void clearFeedbackCaches() const;

 private:
  std::vector<std::uint8_t> code_;
  std::vector<Value> constants_;
  mutable std::vector<FeedbackSlot> feedbackSlots_;
};

}  // namespace minijs
