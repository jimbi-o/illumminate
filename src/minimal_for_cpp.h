#define ASSERTIONS_ENABLED
//#define SHIP_BUILD
#include "illuminate_logger.h"
#include "illuminate_assert.h"
#include "illuminate.h"
namespace illuminate {
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
  dst.reserve(dst.size() + src.size);
  std::move(std::begin(src), std::end(src), std::back_inserter(dst));
}
}
