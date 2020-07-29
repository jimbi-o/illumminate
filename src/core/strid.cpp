#include "strid.h"
#include <unordered_map>
#include <unordered_set>
#include "doctest/doctest.h"
TEST_CASE("strid") {
  using namespace illuminate::core;
  StrId sid(SID("a"));
  CHECK(sid == SID("a"));
  CHECK(sid != SID("b"));
  switch (sid) {
    case HASH("a"): {
      CHECK(true);
      break;
    }
    case HASH("b"): {
      CHECK(false);
      break;
    }
  }
  std::unordered_map<StrId, uint32_t> map;
  map[SID("c")] = 255;
  CHECK(map[SID("c")] == 255);
  CHECK(!IsContaining(map, SID("d")));
  std::unordered_set<StrId> set;
  set.insert(SID("d"));
  CHECK(IsContaining(set, SID("d")));
}
