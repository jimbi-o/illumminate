#ifndef ILLUMINATE_CORE_MEMORY_H
#define ILLUMINATE_CORE_MEMORY_H
#include <cstdint>
#include <set>
#include <vector>
#include <unordered_map>
namespace illuminate::core {
constexpr inline std::uintptr_t AlignAddress(const std::uintptr_t addr, const size_t align) {
  const auto mask = align - 1;
  //assert((align & mask) == 0);
  return (addr + mask) & ~mask;
}
inline void* AlignAddress(const void* ptr, const size_t align) {
  auto addr = AlignAddress(reinterpret_cast<std::uintptr_t>(ptr), align);
  return reinterpret_cast<void*>(addr);
}
class LinearAllocator {
 public:
  // explicit LinearAllocator(void* const buffer, const size_t size_in_bytes) : head_(reinterpret_cast<uintptr_t>(buffer)), offset_(0), size_in_bytes_(size_in_bytes) {}
   explicit LinearAllocator(void* const buffer, const size_t size_in_bytes) : head_(reinterpret_cast<uintptr_t>(buffer)), offset_(0) {}
  ~LinearAllocator() {}
  inline void* Alloc(const size_t size_in_bytes, const size_t alignment_in_bytes) {
    auto aligned_address = AlignAddress(head_ + offset_, alignment_in_bytes);
    offset_ = aligned_address - head_ + size_in_bytes;
    // assert(offset_ <= size_in_bytes_);
    return reinterpret_cast<void*>(aligned_address);
  }
  inline constexpr void Free() { offset_ = 0; }
  inline constexpr size_t GetOffset() const { return offset_; }
 private:
  LinearAllocator() = delete;
  LinearAllocator& operator=(const LinearAllocator&) = delete;
  std::uintptr_t head_;
  size_t offset_;
  // size_t size_in_bytes_;
};
template <typename T, typename A, size_t size_in_bytes, size_t align>
class Allocator {
 public:
  typedef T value_type;
  typedef size_t size_type;
  typedef ptrdiff_t difference_type;
  typedef std::true_type propagate_on_container_move_assignment;
  typedef std::true_type is_always_equal;
  explicit Allocator(A* const allocator) throw() : allocator_(allocator) { }
  explicit Allocator(const Allocator& a) throw() : allocator_(a.allocator_) {}
  template<typename U>
  explicit Allocator(const Allocator<U, A, sizeof(U), _Alignof(U)>& a) throw() : allocator_(a.GetAllocator()) {}
  ~Allocator() throw() { }
  [[nodiscard]] constexpr T* allocate(std::size_t n) {
    auto ptr = allocator_->Alloc(size_in_bytes * n, align);
    return static_cast<T*>(ptr);
  }
  constexpr void deallocate(T* p, std::size_t n) {}
  template<typename U>
  struct rebind { typedef Allocator<U, A, size_in_bytes, align> other; };
  constexpr A* GetAllocator() const { return allocator_; }
 private:
  Allocator() = delete;
  A* allocator_ = nullptr;
};
template <typename T>
using allocator_t = Allocator<T, LinearAllocator, sizeof(T), _Alignof(T)>;
// Calling resize() or reserve() is recommended before inserting elements due to LinearAllocator.
template <typename T>
using set = std::set<T, allocator_t<T>>;
template <typename T>
using unordered_map = std::unordered_map<T, allocator_t<T>>;
template <typename T>
using vector = std::vector<T, allocator_t<T>>;
template <typename T>
class AllocJanitor {
 public:
  explicit AllocJanitor(void* buffer, const size_t size_in_bytes) : allocator_(buffer, size_in_bytes) {};
  ~AllocJanitor() { allocator_.Free(); }
  template <typename U>
  constexpr allocator_t<U> GetAllocator() { return allocator_t<U>(&allocator_); }
 private:
  T allocator_;
};
using alloc_janitor_t = AllocJanitor<LinearAllocator>;
class MemoryManager {
 public:
  struct MemoryManagerConfig {
    uint32_t frame_num;
    void* buffer_tmp;
    void* buffer_one_frame;
    void** buffer_frame_buffered;
    size_t size_in_bytes_tmp;
    size_t size_in_bytes_one_frame;
    size_t size_in_bytes_frame_buffered;
  };
  explicit MemoryManager(MemoryManagerConfig&& config)
      : config_(std::move(config)), prev_frame_index_(config.frame_num - 1)
  {
    allocator_one_frame_ = new LinearAllocator(config_.buffer_one_frame, config_.size_in_bytes_one_frame);
    allocator_frame_buffered_ = new LinearAllocator*[config_.frame_num];
    for (uint32_t i = 0; i < config_.frame_num; i++) {
      allocator_frame_buffered_[i] = new LinearAllocator(config_.buffer_frame_buffered[i], config_.size_in_bytes_frame_buffered);
    }
  }
  ~MemoryManager() {
    delete allocator_one_frame_;
    for (uint32_t i = 0; i < config_.frame_num; i++) {
      delete allocator_frame_buffered_[i];
    }
    delete[] allocator_frame_buffered_;
  }
  alloc_janitor_t GetAllocatorWorkJanitor() const { return alloc_janitor_t(config_.buffer_tmp, config_.size_in_bytes_tmp); }
  template <typename T>
  constexpr allocator_t<T> GetAllocatorOneFrame() const { return allocator_t<T>(allocator_one_frame_); }
  template <typename T>
  constexpr allocator_t<T> GetAllocatorFrameBuffered() const { return allocator_t<T>(allocator_frame_buffered_[frame_index_]); }
  constexpr uint32_t GetFrameIndex() const { return frame_index_; }
  constexpr uint32_t GetPrevFrameIndex() const { return prev_frame_index_; }
  constexpr void SucceedToNextFrame() { // for frame buffered
    prev_frame_index_ = frame_index_;
    frame_index_++;
    if (frame_index_ >= config_.frame_num) frame_index_ = 0;
    allocator_frame_buffered_[frame_index_]->Free();
    allocator_one_frame_->Free();
  }
  constexpr void* GetCurrentHead() const { // for frame buffered
    return config_.buffer_frame_buffered[frame_index_];
  }
  constexpr void* GetPrevHead() const { // for frame buffered
    return config_.buffer_frame_buffered[prev_frame_index_];
  }
 private:
  MemoryManagerConfig config_;
  LinearAllocator* allocator_one_frame_ = nullptr;
  LinearAllocator** allocator_frame_buffered_ = nullptr;
  uint32_t frame_index_ = 0;
  uint32_t prev_frame_index_ = 0;
};
}
#endif
