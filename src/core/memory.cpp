#include "illuminate/illuminate.h"
#include <memory>
#include <vector>
#include "doctest/doctest.h"
TEST_CASE("memory") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  const uint32_t size_in_byte = 1024;
  std::byte buffer[size_in_byte]{};
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, size_in_byte);
  CHECK(memory_resource->allocate(size_in_byte, 4) == buffer); // NOLINT
  CHECK(memory_resource->GetOffset() == 1024); // NOLINT
  CHECK(memory_resource->allocate(1, 4) == nullptr); // NOLINT
  memory_resource->Reset();
  CHECK(memory_resource->allocate(size_in_byte, 4) == buffer); // NOLINT
  memory_resource->Reset();
  CHECK(memory_resource->allocate(500, 4) == buffer); // NOLINT
  CHECK(memory_resource->GetOffset() == 500); // NOLINT
  std::pmr::vector<uint32_t> v(memory_resource.get());
  v.push_back(0);
  CHECK(memory_resource->GetOffset() > 500); // NOLINT
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  CHECK(v[0] == 0); // NOLINT
  CHECK(v[1] == 1); // NOLINT
  CHECK(v[2] == 2); // NOLINT
  CHECK(v[3] == 3); // NOLINT
  auto v_offset = memory_resource->GetOffset();
  std::pmr::vector<uint32_t> v2(memory_resource.get());
  v2.push_back(10);
  CHECK(memory_resource->GetOffset() > v_offset); // NOLINT
  v2.push_back(11);
  v2.push_back(12);
  v2.push_back(13);
  CHECK(v2[0] == 10); // NOLINT
  CHECK(v2[1] == 11); // NOLINT
  CHECK(v2[2] == 12); // NOLINT
  CHECK(v2[3] == 13); // NOLINT
  CHECK(v[0] == 0); // NOLINT
  CHECK(v[1] == 1); // NOLINT
  CHECK(v[2] == 2); // NOLINT
  CHECK(v[3] == 3); // NOLINT
  v.reserve(100);
  v.push_back(101);
  CHECK(v2[0] == 10); // NOLINT
  CHECK(v2[1] == 11); // NOLINT
  CHECK(v2[2] == 12); // NOLINT
  CHECK(v2[3] == 13); // NOLINT
  CHECK(v[0] == 0); // NOLINT
  CHECK(v[1] == 1); // NOLINT
  CHECK(v[2] == 2); // NOLINT
  CHECK(v[3] == 3); // NOLINT
  CHECK(v[4] == 101); // NOLINT
}
