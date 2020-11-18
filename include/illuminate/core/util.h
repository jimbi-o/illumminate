#ifndef ILLUMINATE_CORE_UTIL_H
#define ILLUMINATE_CORE_UTIL_H
#include <cstdint>
#include <iterator>
namespace illuminate {
enum class EnableDisable : uint8_t { kEnabled = 0, kDisabled };
constexpr bool IsEnabled(const EnableDisable e) { return e == EnableDisable::kEnabled; }
template <typename T, typename U>
constexpr bool IsContaining(const std::vector<T>& vec, const U& val) {
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
}
#endif
