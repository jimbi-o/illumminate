#include "illuminate.h"
#include <memory>
#include <vector>
#include "doctest/doctest.h"
TEST_CASE("memory") {
  using namespace illuminate;
  using namespace illuminate::core;
  const uint32_t size_in_byte = 1024;
  std::byte buffer[size_in_byte]{};
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  CHECK(memory_resource->allocate(size_in_byte, 4) == buffer);
  CHECK(memory_resource->GetOffset() == 1024);
  CHECK(memory_resource->allocate(1, 4) == nullptr);
  memory_resource->Reset();
  CHECK(memory_resource->allocate(size_in_byte, 4) == buffer);
  memory_resource->Reset();
  CHECK(memory_resource->allocate(500, 4) == buffer);
  CHECK(memory_resource->GetOffset() == 500);
  std::pmr::vector<uint32_t> v(memory_resource.get());
  v.push_back(0);
  CHECK(memory_resource->GetOffset() > 500);
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  CHECK(v[0] == 0);
  CHECK(v[1] == 1);
  CHECK(v[2] == 2);
  CHECK(v[3] == 3);
  auto v_offset = memory_resource->GetOffset();
  std::pmr::vector<uint32_t> v2(memory_resource.get());
  v2.push_back(10);
  CHECK(memory_resource->GetOffset() > v_offset);
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