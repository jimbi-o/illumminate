#include "strid.h"
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
using size_t = std::size_t;
namespace illuminate::core {
template <typename C, typename V>
bool IsContaining(const C& container, const V& val) {
  return container.find(val) != container.end();
}
using HashResult = uint32_t;
// https://xueyouchao.github.io/2016/11/16/CompileTimeString/
template <size_t N>
constexpr inline HashResult HornerHash(const HashResult prime, const char (&str)[N], const size_t len = N-1)
{
  return (len <= 1) ? str[0] : (prime * HornerHash(prime, str, len-1) + str[len-1]);
}
class StrId {
 public:
  constexpr StrId(const HashResult hash) : hash_(hash) {}
  constexpr operator uint32_t() const { return hash_; }
  constexpr bool operator==(const StrId& id) const { return hash_ == id.hash_; } // for unordered_map
  constexpr HashResult GetHash() const { return hash_; }
 private:
  HashResult hash_;
};
}
// for unordered_map
namespace std {
template <>
struct hash<illuminate::core::StrId> {
  std::size_t operator()(const illuminate::core::StrId& id) const { return id.GetHash(); }
};
template <>
struct less<illuminate::core::StrId> {
  bool operator()(const illuminate::core::StrId& l, const illuminate::core::StrId& r) const { return r.GetHash() < l.GetHash(); }
};
}
#define SID(str) StrId(HornerHash(31,str))
#include "doctest/doctest.h"
TEST_CASE("strid") {
  using namespace illuminate::core;
  StrId sid(SID("a"));
  CHECK(sid == SID("a"));
  CHECK(sid != SID("b"));
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
  map[SID("c")] = 255;
  CHECK(map[SID("c")] == 255);
  CHECK(!IsContaining(map, SID("d")));
  std::unordered_set<StrId> set;
  set.insert(SID("d"));
  CHECK(IsContaining(set, SID("d")));
}
