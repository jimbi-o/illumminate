#ifndef __ILLUMINATE_GFX_DEF_H__
#define __ILLUMINATE_GFX_DEF_H__
#include <cstdint>
#include <unordered_set>
namespace illuminate::gfx {
enum class CommandQueueType : uint8_t { kGraphics, kCompute, kTransfer, };
const std::unordered_set<CommandQueueType> kCommandQueueTypeSet{CommandQueueType::kGraphics, CommandQueueType::kCompute, CommandQueueType::kTransfer};
}
#endif
