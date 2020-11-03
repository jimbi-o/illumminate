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
  constexpr void Reset() { offset_in_byte_ = 0; }
  inline void* Allocate(std::size_t bytes, std::size_t alignment_in_bytes) {
    auto addr_aligned = AlignAddress(head_ + offset_in_byte_, alignment_in_bytes);
    offset_in_byte_ = addr_aligned - head_ + bytes;
    if (offset_in_byte_ > size_in_byte_) return nullptr;
    return reinterpret_cast<void*>(addr_aligned);
  }
  constexpr auto GetOffset() const { return offset_in_byte_; }
 private:
  const std::uintptr_t head_;
  const size_t size_in_byte_;
  size_t offset_in_byte_;
};
class PmrLinearAllocator : public std::pmr::memory_resource {
 public:
  explicit PmrLinearAllocator(const std::byte* buffer, const uint32_t size_in_byte) : allocator_(buffer, size_in_byte) {}
  PmrLinearAllocator() {}
  ~PmrLinearAllocator() override {}
  PmrLinearAllocator(const PmrLinearAllocator&) = delete;
  PmrLinearAllocator& operator=(const PmrLinearAllocator&) = delete;
  constexpr void Reset() { allocator_.Reset(); }
 private:
  inline void* do_allocate(std::size_t bytes, std::size_t alignment_in_bytes) override {
    return allocator_.Allocate(bytes, alignment_in_bytes);
  }
  constexpr inline void do_deallocate([[maybe_unused]] void* p, [[maybe_unused]] std::size_t bytes, [[maybe_unused]] std::size_t alignment) override {}
  bool do_is_equal(const memory_resource& other) const noexcept override;
  LinearAllocator allocator_;
};
bool PmrLinearAllocator::do_is_equal(const memory_resource& other) const noexcept {
  return this == &other;
}
}
#include <memory>
#include <vector>
#include "doctest/doctest.h"
TEST_CASE("memory") {
  using namespace illuminate;
  using namespace illuminate::core;
  const uint32_t size_in_byte = 1024;
  std::byte buffer[size_in_byte]{};
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  std::pmr::vector<uint32_t> v(memory_resource.get());
  v.reserve(3);
  v.push_back(0);
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  CHECK(v[0] == 0);
  CHECK(v[1] == 1);
  CHECK(v[2] == 2);
  CHECK(v[3] == 3);
  memory_resource->Reset();
  CHECK(memory_resource->allocate(size_in_byte, 4) == buffer);
  CHECK(memory_resource->allocate(1, 4) == nullptr);
  memory_resource->Reset();
  CHECK(memory_resource->allocate(size_in_byte, 4) == buffer);
  memory_resource->Reset();
  CHECK(memory_resource->allocate(500, 4) == buffer);
  v.clear();
  v.push_back(0);
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  CHECK(v[0] == 0);
  CHECK(v[1] == 1);
  CHECK(v[2] == 2);
  CHECK(v[3] == 3);
  std::pmr::vector<uint32_t> v2(memory_resource.get());
  v2.push_back(10);
  v2.push_back(11);
  v2.push_back(12);
  v2.push_back(13);
  CHECK(v2[0] == 10);
  CHECK(v2[1] == 11);
  CHECK(v2[2] == 12);
  CHECK(v2[3] == 13);
  CHECK(v[0] == 0);
  CHECK(v[1] == 1);
  CHECK(v[2] == 2);
  CHECK(v[3] == 3);
  v.reserve(100);
  v.push_back(101);
  CHECK(v2[0] == 10);
  CHECK(v2[1] == 11);
  CHECK(v2[2] == 12);
  CHECK(v2[3] == 13);
  CHECK(v[0] == 0);
  CHECK(v[1] == 1);
  CHECK(v[2] == 2);
  CHECK(v[3] == 3);
  CHECK(v[4] == 101);
}
