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
template <typename T>
using set = std::pmr::set<T>;
template <typename K, typename V>
using unordered_map = std::pmr::unordered_map<K,V>;
template <typename K, typename V>
using map = std::pmr::map<K,V>;
template <typename T>
using vector = std::pmr::vector<T>;
using PassId = StrId;
using BufferId = uint32_t;
enum BufferStateFlags : uint32_t {
  kBufferStateFlagNone      = 0x0000,
  kBufferStateFlagCbvUpload = 0x0001,
  kBufferStateFlagSrvPsOnly = 0x0002,
  kBufferStateFlagSrvNonPs  = 0x0004,
  kBufferStateFlagUavRead   = 0x0008,
  kBufferStateFlagUavWrite  = 0x0010,
  kBufferStateFlagRtv       = 0x0020,
  kBufferStateFlagDsvWrite  = 0x0040,
  kBufferStateFlagDsvRead   = 0x0080,
  kBufferStateFlagCopySrc   = 0x0100,
  kBufferStateFlagCopyDst   = 0x0200,
  kBufferStateFlagPresent   = 0x0400,
  kBufferStateFlagCommon    = kBufferStateFlagPresent,
};
constexpr bool IsBufferStateFlagMergeAcceptable(const BufferStateFlags& state) {
  if (state & kBufferStateFlagUavRead) return false;
  if (state & kBufferStateFlagUavWrite) return false;
  if (state & kBufferStateFlagRtv) return false;
  if (state & kBufferStateFlagDsvWrite) return false;
  if (state & kBufferStateFlagCopyDst) return false;
  if (state & kBufferStateFlagPresent) return false;
  return true;
}
constexpr bool IsBufferStateFlagsMergeable(const BufferStateFlags& a, const BufferStateFlags& b) {
  if (!IsBufferStateFlagMergeAcceptable(a)) return false;
  if (!IsBufferStateFlagMergeAcceptable(b)) return false;
  return true;
}
constexpr BufferStateFlags MergeBufferStateFlags(const BufferStateFlags& a, const BufferStateFlags& b) {
  return static_cast<BufferStateFlags>(a | b);
}
struct BufferConfig {
  uint32_t width;
  uint32_t height;
  BufferDimensionType dimension;
  BufferFormat format;
  BufferStateFlags state_flags;
  BufferStateFlags initial_state_flags;
  ClearValue clear_value;
};
enum class BarrierSplitType : uint8_t { kNone = 0, kBegin, kEnd, };
struct BufferStateSet {
  BufferId buffer_id;
  BufferStateFlags state;
};
}
#endif
