#pragma once

#include <cstddef>

namespace minijs {

class VM;
struct ObjClosure;

struct BaselineFrame {
  VM* vm = nullptr;
  ObjClosure* closure = nullptr;

  std::size_t returnSlot = 0;
  std::size_t slotStart = 0;

  bool completed = false;
  bool failed = false;
};

using BaselineEntry = void (*)(BaselineFrame*);

}  // namespace minijs
