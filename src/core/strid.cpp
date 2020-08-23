#include "strid.h"
#include <unordered_map>
#include <unordered_set>
#include "minimal_for_cpp.h"
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
  CHECK(!IsContaining(map, StrId("d")));
  std::unordered_set<StrId> set;
  set.insert(StrId("d"));
  CHECK(IsContaining(set, StrId("d")));
  CHECK(!IsContaining(set, StrId("d ")));
  auto sid_auto = StrId("b");
  CHECK(sid_auto);
}
