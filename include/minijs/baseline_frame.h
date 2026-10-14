#pragma once

#include <cstddef>
#include <string>

namespace minijs {

class VM;
class Value;
struct ObjClosure;

struct BaselineFrame {
  VM* vm = nullptr;
  ObjClosure* closure = nullptr;

  std::size_t returnSlot = 0;
  std::size_t slotStart = 0;
  Value* stackData = nullptr;
  std::size_t stackSize = 0;

  bool returnsReceiver = false;
  bool completed = false;
  bool failed = false;
  std::string errorMessage;
};

using BaselineEntry = void (*)(BaselineFrame*);

}  // namespace minijs
