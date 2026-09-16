#include "minijs/executable_memory.h"

#include <cerrno>
#include <cstring>
#include <utility>

#if defined(_WIN32)
// ---------- Windows 平台 ----------
// 防止 <windows.h> 定义 min/max 宏，避免与 std::min/std::max 冲突
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
// ---------- POSIX / Unix 平台 ----------
#include <sys/mman.h>
#include <unistd.h>
#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

namespace minijs {
namespace {

// 把请求的内存大小向上对齐到操作系统的页大小（mmap/VirtualAlloc 都按页管理权限）
std::size_t allocationSizeFor(std::size_t size) {
 // 1. 获取页大小
#if defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  const std::size_t pageSize = info.dwPageSize;
#else
  const long pageSizeValue = sysconf(_SC_PAGESIZE);
  const std::size_t pageSize = pageSizeValue > 0 ? static_cast<std::size_t>(pageSizeValue) : 4096;
#endif
    
  // 2. 向上对齐
  return ((size + pageSize - 1) / pageSize) * pageSize;
}

}  // namespace

ExecutableMemory::ExecutableMemory(void* data, std::size_t size, std::size_t allocationSize)
    : data_(data), size_(size), allocationSize_(allocationSize) {}

ExecutableMemory::ExecutableMemory(ExecutableMemory&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      allocationSize_(std::exchange(other.allocationSize_, 0)) {}

ExecutableMemory& ExecutableMemory::operator=(ExecutableMemory&& other) noexcept {
  if (this == &other) {
    return *this;
  }

  release();
  data_ = std::exchange(other.data_, nullptr);
  size_ = std::exchange(other.size_, 0);
  allocationSize_ = std::exchange(other.allocationSize_, 0);
  return *this;
}

ExecutableMemory::~ExecutableMemory() { release(); }

// release() 保持 move assignment 和析构走同一条释放路径，避免重复维护平台分支。
void ExecutableMemory::release() {
  if (data_ == nullptr) {
    return;
  }

#if defined(_WIN32)
  VirtualFree(data_, 0, MEM_RELEASE);
#else
  munmap(data_, allocationSize_);
#endif

  data_ = nullptr;
  size_ = 0;
  allocationSize_ = 0;
}

// 先以 RW 写入机器码，再切成 RX，保持 W^X。写入后刷新 instruction cache，
// 避免 CPU 继续执行旧的 cache line。
std::shared_ptr<ExecutableMemory> ExecutableMemory::allocate(
    const std::vector<std::uint8_t>& bytes, std::string& error) {
  if (bytes.empty()) {
    error = "cannot allocate empty executable memory";
    return nullptr;
  }

  const std::size_t allocationSize = allocationSizeFor(bytes.size());

#if defined(_WIN32)
    // ============================================================
    // Windows
    // ============================================================
    
    // 1. 申请可读写内存（先 RW，后改 RX，避免 W^X 问题）
  void* memory =
      VirtualAlloc(nullptr, allocationSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  if (memory == nullptr) {
    error = "VirtualAlloc failed";
    return nullptr;
  }

    // 2. 拷贝机器码
  std::memcpy(memory, bytes.data(), bytes.size());
    // 3. 刷新指令缓存（x86 上通常无操作，但语义上必须）
  FlushInstructionCache(GetCurrentProcess(), memory, bytes.size());

    // 4. 改为可执行
  DWORD oldProtect = 0;
  if (VirtualProtect(memory, allocationSize, PAGE_EXECUTE_READ, &oldProtect) == 0) {
    VirtualFree(memory, 0, MEM_RELEASE);
    error = "VirtualProtect failed";
    return nullptr;
  }
#else
    // ============================================================
    // POSIX (Linux / macOS / BSD)
    // ============================================================
    
    // 1. 申请可读写匿名映射
  void* memory =
      mmap(nullptr, allocationSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (memory == MAP_FAILED) {
    error = std::string("mmap failed: ") + std::strerror(errno);
    return nullptr;
  }

    // 2. 拷贝机器码
  std::memcpy(memory, bytes.data(), bytes.size());
    // 3. 刷新指令缓存（ARM 等架构必需，x86 上无操作）
#if defined(__GNUC__) || defined(__clang__)
  __builtin___clear_cache(static_cast<char*>(memory),
                          static_cast<char*>(memory) + bytes.size());
#endif

    // 4. 改为可执行
  if (mprotect(memory, allocationSize, PROT_READ | PROT_EXEC) != 0) {
    error = std::string("mprotect failed: ") + std::strerror(errno);
    munmap(memory, allocationSize);
    return nullptr;
  }
#endif
    // ---------- 交给 RAII 管理 ----------
  return std::shared_ptr<ExecutableMemory>(
      new ExecutableMemory(memory, bytes.size(), allocationSize));
}

}  // namespace minijs
