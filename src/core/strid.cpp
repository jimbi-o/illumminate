#include "illuminate/illuminate.h"
#ifdef ENABLE_STRID_DEBUG_STR
namespace illuminate::core {
char StrId::debug_buffer[1024]{};
uint32_t StrId::debug_buffer_index{};
}
#endif
#include <unordered_map>
#include <unordered_set>
#include "doctest/doctest.h"
TEST_CASE("strid") {
  using namespace illuminate;
  using namespace illuminate::core;
  StrId sid("a");
  CHECK(sid == StrId("a"));
  CHECK(sid != StrId("b"));
  StrId sid_b("b");
  CHECK(sid_b != StrId("a"));
  CHECK(sid_b == StrId("b"));
  switch (sid) {
    case SID("a"): {
      CHECK(true);
      break;
    }
    case SID("b"): {
      CHECK(false);
      break;
    }
  }
  std::unordered_map<StrId, uint32_t> map;
  map[StrId("c")] = 255;
  CHECK(map[StrId("c")] == 255);
  CHECK(!map.contains(StrId("d")));
  std::unordered_set<StrId> set;
  set.insert(StrId("d"));
  CHECK(set.contains(StrId("d")));
  CHECK(!set.contains(StrId("d ")));
  auto sid_auto = StrId("b");
  CHECK(sid_auto);
}
