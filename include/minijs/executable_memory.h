#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minijs {

class ExecutableMemory {
 public:
  ExecutableMemory() = default;
  ExecutableMemory(const ExecutableMemory&) = delete;
  ExecutableMemory& operator=(const ExecutableMemory&) = delete;
  ExecutableMemory(ExecutableMemory&& other) noexcept;
  ExecutableMemory& operator=(ExecutableMemory&& other) noexcept;
  ~ExecutableMemory();

  static std::shared_ptr<ExecutableMemory> allocate(const std::vector<std::uint8_t>& bytes,
                                                    std::string& error);

  void* data() const { return data_; }
  std::size_t size() const { return size_; }

 private:
  ExecutableMemory(void* data, std::size_t size, std::size_t allocationSize);
  void release();

  void* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t allocationSize_ = 0;
};

}  // namespace minijs
