#ifndef __ILLUMINATE_GFX_DEF_H__
#define __ILLUMINATE_GFX_DEF_H__
#include <cstdint>
namespace illuminate::gfx {
enum class CommandListType : uint8_t { kGraphics, kCompute, kTransfer, };
}
#endif
