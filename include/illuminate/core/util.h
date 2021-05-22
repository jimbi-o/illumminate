#ifndef ILLUMINATE_CORE_UTIL_H
#define ILLUMINATE_CORE_UTIL_H
#include <cstdint>
#include <iterator>
#include <unordered_set>
namespace illuminate::core {
enum class EnableDisable : uint8_t { kEnabled = 0, kDisabled };
constexpr bool IsEnabled(const EnableDisable e) { return e == EnableDisable::kEnabled; }
template <typename T, typename U>
constexpr bool IsContaining(const std::pmr::vector<T>& vec, const U& val) {
  return std::find(vec.begin(), vec.end(), val) != vec.end();
}
template <typename T, typename U>
constexpr void AppendVector(T&& src, U&& dst) {
  if (dst.empty()) {
    dst = std::move(src);
    return;
  }
  dst.reserve(dst.size() + src.size());
  std::move(std::begin(src), std::end(src), std::back_inserter(dst));
}
constexpr uint32_t CountSetBitNum(const uint32_t& val) {
  uint32_t v = val;
  uint32_t count = 0;
  while (v != 0) {
    if (v & 1) {
      count++;
    }
    v = (v >> 1);
  }
  return count;
}
template <typename K, typename V>
constexpr auto CreateValueSetFromMap(const std::pmr::unordered_map<K,V>& map, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<V> ret{memory_resource};
  ret.reserve(map.size());
  for (auto& [k,v] : map) {
    ret.insert(v);
  }
  return ret;
}
template <typename K, typename V>
constexpr bool IsContainingValue(const std::pmr::unordered_map<K, V>& map, const V& v) {
  // consider using std::find_if 
  for (auto& [key, value] : map) {
    if (value == v) return true;
  }
  return false;
}
}
#endif
