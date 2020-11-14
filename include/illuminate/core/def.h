#ifndef ILLUMINATE_CORE_DEF_H
#define ILLUMINATE_CORE_DEF_H
#include <cstdint>
namespace illuminate::core {
enum class EnableDisable : uint8_t { kEnabled = 0, kDisabled };
constexpr bool IsEnabled(const EnableDisable e) { return e == EnableDisable::kEnabled; }
}
#endif
