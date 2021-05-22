#include "illuminate/illuminate.h"
#ifdef ENABLE_STRID_DEBUG_STR
namespace illuminate::core {
std::array<char, StrId::debug_buffer_len> StrId::debug_buffer{};
uint32_t StrId::debug_buffer_index{};
} // namespace illuminate::core
#endif
#include "doctest/doctest.h"
#include <unordered_map>
#include <unordered_set>
TEST_CASE("strid") { // NOLINT
  using illuminate::core::StrId;
  StrId sid("a");
  CHECK(sid == StrId("a")); // NOLINT
  CHECK(sid != StrId("b")); // NOLINT
  StrId sid_b("b");
  CHECK(sid_b != StrId("a")); // NOLINT
  CHECK(sid_b == StrId("b")); // NOLINT
  switch (sid) {
    case SID("a"): { // NOLINT(fuchsia-default-arguments-calls)
      CHECK(true); // NOLINT
      break;
    }
    case SID("b"): { // NOLINT(fuchsia-default-arguments-calls)
      CHECK(false); // NOLINT
      break;
    }
  }
  std::unordered_map<StrId, uint32_t> map;
  map[StrId("c")] = 255; // NOLINT
  CHECK(map[StrId("c")] == 255); // NOLINT
  CHECK(!map.contains(StrId("d"))); // NOLINT
  std::unordered_set<StrId> set;
  set.insert(StrId("d"));
  CHECK(set.contains(StrId("d"))); // NOLINT
  CHECK(!set.contains(StrId("d "))); // NOLINT
  auto sid_auto = StrId("b");
  CHECK(sid_auto); // NOLINT
}
