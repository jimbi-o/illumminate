#ifndef ILLUMINATE_RENDER_GRAPH_H
#define ILLUMINATE_RENDER_GRAPH_H
#include <map>
#include <set>
#include <vector>
#include "gfx_def.h"
#include "illuminate/illuminate.h"
namespace illuminate::gfx {
template <typename T>
using unordered_set = std::pmr::unordered_set<T>;
template <typename K, typename V>
using unordered_map = std::pmr::unordered_map<K,V>;
template <typename T>
using vector = std::pmr::vector<T>;
using PassId = StrId;
using BufferId = uint32_t;
enum BufferStateFlags : uint32_t {
  kBufferStateFlagNone      = 0x0000,
  kBufferStateFlagCbvUpload = 0x0001,
  kBufferStateFlagSrvPsOnly = 0x0002,
  kBufferStateFlagSrvNonPs  = 0x0004,
  kBufferStateFlagUav       = 0x0008,
  kBufferStateFlagRtv       = 0x0010,
  kBufferStateFlagDsvWrite  = 0x0020,
  kBufferStateFlagDsvRead   = 0x0040,
  kBufferStateFlagCopySrc   = 0x0080,
  kBufferStateFlagCopyDst   = 0x0100,
  kBufferStateFlagPresent   = 0x0200,
  kBufferStateFlagCommon    = kBufferStateFlagPresent,
};
struct BufferConfig {
  uint32_t width;
  uint32_t height;
  BufferDimensionType dimension;
  BufferFormat format;
  BufferStateFlags state_flags;
  BufferStateFlags initial_state_flags;
  ClearValue clear_value;
};
}
#endif
