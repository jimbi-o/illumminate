#include "illuminate_memory.h"
#include "doctest/doctest.h"
TEST_CASE("align address") {
  using namespace illuminate::core;
  for (size_t align = 1; align <= 256; align *= 2) {
    CAPTURE(align);
    std::uintptr_t addr = align * 16;
    do {
      CAPTURE(addr);
      const auto aligned_addr = AlignAddress(addr, align);
      CHECK(addr <= aligned_addr);
      CHECK(aligned_addr - addr <= align);
      CHECK(aligned_addr % align == 0);
      ++addr;
    } while(addr % align != 0);
  }
}
TEST_CASE("linear allocator") {
  using namespace illuminate::core;
  uint8_t buffer[1024]{};
  LinearAllocator allocator(buffer, 1024);
  size_t alloc_size = 4, alignment = 64;
  auto ptr = allocator.Alloc(alloc_size, alignment);
  CHECK(allocator.GetOffset() >= alloc_size);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0);
  CHECK(ptr);
  auto prev_offset = allocator.GetOffset();
  auto prev_ptr = reinterpret_cast<std::uintptr_t>(ptr);
  auto prev_alloc_size = alloc_size;
  alloc_size = 32, alignment = 8;
  ptr = allocator.Alloc(alloc_size, alignment);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) - prev_ptr >= prev_alloc_size);
  CHECK(allocator.GetOffset() - prev_offset >= alloc_size);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0);
  CHECK(ptr);
  prev_offset = allocator.GetOffset();
  prev_ptr = reinterpret_cast<std::uintptr_t>(ptr);
  prev_alloc_size = alloc_size;
  alloc_size = 256, alignment = 16;
  ptr = allocator.Alloc(alloc_size, alignment);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) - prev_ptr >= prev_alloc_size);
  CHECK(allocator.GetOffset() - prev_offset >= alloc_size);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0);
  CHECK(ptr);
  allocator.Free();
  CHECK(allocator.GetOffset() == 0);
  alloc_size = 4, alignment = 64;
  ptr = allocator.Alloc(alloc_size, alignment);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) - prev_ptr >= prev_alloc_size);
  CHECK(allocator.GetOffset() >= alloc_size);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0);
  CHECK(ptr);
  prev_offset = allocator.GetOffset();
  prev_ptr = reinterpret_cast<std::uintptr_t>(ptr);
  prev_alloc_size = alloc_size;
  alloc_size = 32, alignment = 8;
  ptr = allocator.Alloc(alloc_size, alignment);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) - prev_ptr >= prev_alloc_size);
  CHECK(allocator.GetOffset() - prev_offset >= alloc_size);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0);
  CHECK(ptr);
  prev_offset = allocator.GetOffset();
  prev_ptr = reinterpret_cast<std::uintptr_t>(ptr);
  prev_alloc_size = alloc_size;
  alloc_size = 256, alignment = 16;
  ptr = allocator.Alloc(alloc_size, alignment);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) - prev_ptr >= prev_alloc_size);
  CHECK(allocator.GetOffset() - prev_offset >= alloc_size);
  CHECK(reinterpret_cast<std::uintptr_t>(ptr) % alignment == 0);
  CHECK(ptr);
}
TEST_CASE("allocator_t") {
  using namespace illuminate::core;
  uint8_t buffer[1024]{};
  LinearAllocator la(buffer, 1024);
  allocator_t<uint32_t> a(&la);
  auto ptr = a.allocate(1);
  auto ptr2 = a.allocate(2);
  auto ptr3 = a.allocate(5);
  *ptr = 1;
  ptr2[0] = 2;
  ptr2[1] = 3;
  ptr3[0] = 4;
  ptr3[1] = 5;
  ptr3[2] = 6;
  ptr3[3] = 7;
  ptr3[4] = 8;
  for (uint32_t i = 0; i < 8; i++) {
    CAPTURE(i);
    CHECK(ptr[i] == i + 1);
  }
}
TEST_CASE("allocator_t with single vector") {
  using namespace illuminate::core;
  uint8_t buffer[1024]{};
  LinearAllocator la(buffer, 1024);
  allocator_t<uint32_t> a(&la);
  vector<uint32_t> v(a);
  for (uint32_t i = 0; i < 64; i++) {
    v.push_back(i);
  }
  for (uint32_t i = 0; i < 64; i++) {
    CAPTURE(i);
    CHECK(v[i] == i);
  }
}
TEST_CASE("allocator_t with multiple vector") {
  using namespace illuminate::core;
  uint8_t buffer[10 * 1024]{};
  LinearAllocator la(buffer, 10 * 1024);
  allocator_t<uint32_t> a(&la);
  vector<uint32_t> v1(a);
  vector<uint32_t> v2(a);
  for (uint32_t i = 0; i < 64; i++) {
    v1.push_back(i);
    v2.push_back(64-i);
  }
  for (uint32_t i = 0; i < 64; i++) {
    CAPTURE(i);
    CHECK(v1[i] == i);
    CHECK(v2[i] == 64-i);
  }
  while (!v1.empty()) {
    CHECK(v1.back() == v1.size() - 1);
    v1.pop_back();
  }
  while (!v2.empty()) {
    CHECK(v2.back() == 65 - v2.size());
    v2.pop_back();
  }
}
namespace {
illuminate::core::MemoryManager::MemoryManagerConfig CreateTestMemoryManagerConfig() {
  const uint32_t size_in_bytes_tmp = 16 * 1024;
  const uint32_t size_in_bytes_one_frame = 16 * 1024;
  const uint32_t size_in_bytes_frame_buffered = 16 * 1024;
  static uint8_t buffer_tmp[size_in_bytes_tmp]{};
  static uint8_t buffer_one_frame[size_in_bytes_one_frame]{};
  static uint8_t buffer_frame_buffered1[size_in_bytes_frame_buffered]{};
  static uint8_t buffer_frame_buffered2[size_in_bytes_frame_buffered]{};
  static void* buffer_frame_buffered[2]{buffer_frame_buffered1, buffer_frame_buffered2};
  using namespace illuminate::core;
  MemoryManager::MemoryManagerConfig config{};
  config.frame_num = 2;
  config.buffer_tmp = buffer_tmp;
  config.buffer_one_frame = buffer_one_frame;
  config.buffer_frame_buffered = buffer_frame_buffered;
  config.size_in_bytes_tmp = size_in_bytes_tmp;
  config.size_in_bytes_one_frame = size_in_bytes_one_frame;
  config.size_in_bytes_frame_buffered = size_in_bytes_frame_buffered;
  return config;
}
}
TEST_CASE("buffer released when out of scope") {
  using namespace illuminate::core;
  uint8_t buffer[16 * 1024]{};
  void* ptr = nullptr;
  {
    alloc_janitor_t janitor(buffer, 16*1024);
    vector<uint32_t> vec(janitor.GetAllocator<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    CHECK(vec.back() == 9);
    ptr = vec.data();
  }
  {
    alloc_janitor_t janitor(buffer, 16*1024);
    allocator_t<uint32_t> a(janitor.GetAllocator<uint32_t>());
    vector<uint32_t> vec(a);
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i*10);
    }
    CHECK(vec.back() == 90);
    CHECK(ptr == vec.data());
    vector<uint32_t> vec2(a);
    for (uint32_t i = 0; i < 10; i++) {
      vec2.push_back(i*10);
    }
    CHECK(vec2.back() == 90);
    CHECK(vec2.data() > vec.data());
  }
}
TEST_CASE("scoped memory allocation with janitor") {
  using namespace illuminate::core;
  MemoryManager memory(CreateTestMemoryManagerConfig());
  void* ptr = nullptr;
  {
    auto janitor = memory.GetAllocatorWorkJanitor();
    vector<uint32_t> vec(janitor.GetAllocator<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    CHECK(vec.back() == 9);
    ptr = vec.data();
  }
  {
    auto janitor = memory.GetAllocatorWorkJanitor();
    vector<uint32_t> vec(janitor.GetAllocator<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i*10);
    }
    CHECK(vec.back() == 90);
    CHECK(ptr == vec.data());
    vector<uint32_t> vec2(janitor.GetAllocator<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec2.push_back(i*10);
    }
    for (uint32_t i = 0; i < 10; i++) {
      CAPTURE(i);
      CHECK(vec[i] == i * 10);
      CHECK(vec2[i] == i * 10);
    }
    CHECK(vec2.data() > vec.data());
  }
}
TEST_CASE("one frame allocator") {
  using namespace illuminate::core;
  MemoryManager memory(CreateTestMemoryManagerConfig());
  uint32_t* ptr = nullptr;
  {
    vector<uint32_t> vec(memory.GetAllocatorOneFrame<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    ptr = vec.data();
  }
  uint32_t* ptr2 = nullptr;
  {
    vector<uint32_t> vec(memory.GetAllocatorOneFrame<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(100);
    }
    ptr2 = &vec.back();
  }
  CHECK(ptr[9] == 9);
  CHECK(*ptr2 == 100);
}
TEST_CASE("frame success - one frame") {
  using namespace illuminate::core;
  MemoryManager memory(CreateTestMemoryManagerConfig());
  uint32_t* ptr = nullptr;
  {
    vector<uint32_t> vec(memory.GetAllocatorOneFrame<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    ptr = vec.data();
  }
  {
    vector<uint32_t> vec(memory.GetAllocatorOneFrame<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    CHECK(ptr < vec.data());
  }
  memory.SucceedToNextFrame();
  {
    vector<uint32_t> vec(memory.GetAllocatorOneFrame<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    CHECK(ptr == vec.data());
  }
}
TEST_CASE("frame success - frame buffered") {
  using namespace illuminate::core;
  MemoryManager memory(CreateTestMemoryManagerConfig());
  static_cast<uint32_t*>(memory.GetCurrentHead())[9] = 255;
  CHECK(static_cast<uint32_t*>(memory.GetCurrentHead())[9] == 255);
  memory.SucceedToNextFrame();
  static_cast<uint32_t*>(memory.GetCurrentHead())[9] = 1024;
  CHECK(static_cast<uint32_t*>(memory.GetCurrentHead())[9] == 1024);
  CHECK(static_cast<uint32_t*>(memory.GetPrevHead())[9] == 255);
  memory.SucceedToNextFrame();
  CHECK(static_cast<uint32_t*>(memory.GetPrevHead())[9] == 1024);
}
TEST_CASE("frame success - frame buffered with allocator") {
  using namespace illuminate::core;
  MemoryManager memory(CreateTestMemoryManagerConfig());
  uint32_t* ptr = nullptr;
  {
    vector<uint32_t> vec(memory.GetAllocatorFrameBuffered<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    ptr = vec.data();
  }
  {
    vector<uint32_t> vec(memory.GetAllocatorFrameBuffered<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    CHECK(ptr < vec.data());
  }
  memory.SucceedToNextFrame();
  {
    vector<uint32_t> vec(memory.GetAllocatorFrameBuffered<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    CHECK(ptr != vec.data());
  }
  memory.SucceedToNextFrame();
  {
    vector<uint32_t> vec(memory.GetAllocatorFrameBuffered<uint32_t>());
    for (uint32_t i = 0; i < 10; i++) {
      vec.push_back(i);
    }
    CHECK(ptr == vec.data());
  }
}
