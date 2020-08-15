#define ASSERTIONS_ENABLED
//#define SHIP_BUILD
#include "illuminate_logger.h"
#include "illuminate_assert.h"
#include "core/strid.h"
#include "core/illuminate_memory.h"
namespace illuminate {
template <typename C, typename V>
constexpr bool IsContaining(const C& container, const V& val) {
  return container.find(val) != container.end();
}
}
