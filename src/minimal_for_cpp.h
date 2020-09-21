#define ASSERTIONS_ENABLED
//#define SHIP_BUILD
#include "illuminate_logger.h"
#include "illuminate_assert.h"
#include "core/strid.h"
namespace illuminate {
template <typename T, typename U>
constexpr bool IsContaining(const std::vector<T>& vec, const U& val) {
  return std::find(vec.begin(), vec.end(), val) != vec.end();
}
}
