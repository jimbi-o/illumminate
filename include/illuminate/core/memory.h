#ifndef ILLUMINATE_CORE_MEMORY_H
#define ILLUMINATE_CORE_MEMORY_H
#include <memory_resource>
namespace illuminate::core {
constexpr inline std::uintptr_t AlignAddress(const std::uintptr_t addr, const size_t align/*power of 2*/) {
  const auto mask = align - 1;
  return (addr + mask) & ~mask;
}
class LinearAllocator {
 public:
  explicit LinearAllocator(const std::byte* buffer, const uint32_t size_in_byte) : head_(reinterpret_cast<uintptr_t>(buffer)), size_in_byte_(size_in_byte), offset_in_byte_(0) {}
  LinearAllocator() : head_(0), size_in_byte_(0), offset_in_byte_(0) {}
  ~LinearAllocator() {}
  LinearAllocator(const LinearAllocator&) = delete;
  LinearAllocator& operator=(const LinearAllocator&) = delete;
  inline void* Allocate(std::size_t bytes, std::size_t alignment_in_bytes) {
    auto addr_aligned = AlignAddress(head_ + offset_in_byte_, alignment_in_bytes);
    offset_in_byte_ = addr_aligned - head_ + bytes;
    if (offset_in_byte_ > size_in_byte_) return nullptr;
    return reinterpret_cast<void*>(addr_aligned);
  }
  constexpr auto GetOffset() const { return offset_in_byte_; }
  constexpr void Reset() { offset_in_byte_ = 0; }
  constexpr auto GetBufferSizeInByte() const { return size_in_byte_; }
 private:
  const std::uintptr_t head_;
  const size_t size_in_byte_;
  size_t offset_in_byte_;
};
class DoubleBufferedAllocator {
 public:
  explicit DoubleBufferedAllocator(const std::byte* buffer1, const std::byte* buffer2, const uint32_t size_in_byte) : head_{reinterpret_cast<uintptr_t>(buffer1), reinterpret_cast<uintptr_t>(buffer2)}, size_in_byte_(size_in_byte), offset_in_byte_(0), head_index_(0) {}
  DoubleBufferedAllocator() : head_{}, size_in_byte_(0), offset_in_byte_(0), head_index_(0) {}
  ~DoubleBufferedAllocator() {}
  DoubleBufferedAllocator(const DoubleBufferedAllocator&) = delete;
  DoubleBufferedAllocator& operator=(const DoubleBufferedAllocator&) = delete;
  inline void* Allocate(std::size_t bytes, std::size_t alignment_in_bytes) {
    auto addr_aligned = AlignAddress(head_[head_index_] + offset_in_byte_, alignment_in_bytes);
    offset_in_byte_ = addr_aligned - head_[head_index_] + bytes;
    if (offset_in_byte_ > size_in_byte_) return nullptr;
    return reinterpret_cast<void*>(addr_aligned);
  }
  constexpr auto GetOffset() const { return offset_in_byte_; }
  constexpr void Reset() { (head_index_ == 0) ? head_index_ = 1 : head_index_ = 0; offset_in_byte_ = 0; }
  constexpr auto GetBufferSizeInByte() const { return size_in_byte_; }
 private:
  const std::uintptr_t head_[2];
  const size_t size_in_byte_;
  size_t offset_in_byte_;
  uint32_t head_index_;
};
template<class Allocator>
class PmrAllocator : public std::pmr::memory_resource {
 public:
  explicit PmrAllocator(const std::byte* buffer, const uint32_t size_in_byte) : allocator_(buffer, size_in_byte) {}
  explicit PmrAllocator(const std::byte* buffer1, const std::byte* buffer2, const uint32_t size_in_byte) : allocator_(buffer1, buffer2, size_in_byte) {}
  PmrAllocator() {}
  ~PmrAllocator() override {}
  PmrAllocator(const PmrAllocator&) = delete;
  PmrAllocator& operator=(const PmrAllocator&) = delete;
  constexpr void Reset() { allocator_.Reset(); }
  constexpr auto GetOffset() const { return allocator_.GetOffset(); }
  constexpr auto GetBufferSizeInByte() const { return allocator_.GetBufferSizeInByte(); }
 private:
  inline void* do_allocate(std::size_t bytes, std::size_t alignment_in_bytes) override {
    return allocator_.Allocate(bytes, alignment_in_bytes);
  }
  constexpr inline void do_deallocate([[maybe_unused]] void* p, [[maybe_unused]] std::size_t bytes, [[maybe_unused]] std::size_t alignment) override {}
  constexpr bool do_is_equal(const memory_resource& other) const noexcept override { return this == &other; }
  Allocator allocator_;
};
}
using PmrLinearAllocator = illuminate::core::PmrAllocator<illuminate::core::LinearAllocator>;
using PmrDoubleBufferedAllocator = illuminate::core::PmrAllocator<illuminate::core::DoubleBufferedAllocator>;
#endif
