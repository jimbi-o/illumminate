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
  kBufferStateFlagUav       = 0x0008,
  kBufferStateFlagRtv       = 0x0010,
  kBufferStateFlagDsvWrite  = 0x0020,
  kBufferStateFlagDsvRead   = 0x0040,
  kBufferStateFlagCopySrc   = 0x0080,
  kBufferStateFlagCopyDst   = 0x0100,
  kBufferStateFlagPresent   = 0x0200,
  kBufferStateFlagCommon    = kBufferStateFlagPresent,
  kBufferStateReadFlag      = 0x10000000,
  kBufferStateWriteFlag     = 0x20000000,
  kBufferStateFlagUavRead   = (kBufferStateFlagUav | kBufferStateReadFlag),
  kBufferStateFlagUavWrite  = (kBufferStateFlagUav | kBufferStateWriteFlag),
  kBufferStateFlagUavRW     = (kBufferStateFlagUavRead | kBufferStateFlagUavWrite),
};
constexpr bool IsBufferStateFlagMergeAcceptable(const BufferStateFlags& state) {
  if (state & kBufferStateFlagUav) return false;
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
enum class DepthStencilFlag : uint8_t { kDefault = 0, kDepthStencilReadOnly, kDepthReadOnly, kStencilReadOnly, };
struct BarrierTransition {
  BufferStateFlags state_before;
  BufferStateFlags state_after;
};
using BarrierUav = std::monostate;
enum class BarrierSplitType : uint8_t { kNone = 0, kBegin, kEnd, };
struct BarrierConfig {
  BufferId buffer_id;
  BarrierSplitType split_type;
  std::byte _pad[3]{};
  std::variant<BarrierTransition, BarrierUav> params;
};
#if 0
struct BufferConfig {
  uint32_t width;
  uint32_t height;
  BufferDimensionType dimension;
  BufferFormat format;
  BufferStateFlags state_flags;
  BufferStateFlags initial_state_flags;
  ClearValue clear_value;
  DepthStencilFlag depth_stencil_flag;
};
struct BufferStateSet {
  BufferId buffer_id;
  BufferStateFlags state;
};
using RenderPassBufferInfo = vector<BufferStateSet>;
using RenderPassBufferInfoList = vector<RenderPassBufferInfo>;
struct BarrierTransition {
  BufferStateFlags state_before;
  BufferStateFlags state_after;
};
enum class BarrierSplitType : uint8_t { kNone = 0, kBegin, kEnd, };
struct BarrierConfig {
  BufferId buffer_id;
  BarrierSplitType split_type;
  std::byte _pad[3]{};
  std::variant<BarrierTransition> params;
};
vector<vector<BarrierConfig>> ConfigureBarrier(const RenderPassBufferInfoList& pass_buffer_info_list, std::pmr::memory_resource* memory_resource_barrier, std::pmr::memory_resource* memory_resource);
using SignalQueueRenderPassInfo = unordered_map<uint32_t, uint32_t>;
enum class BufferReadWriteFlag : uint8_t {
  kRead      = 0x1,
  kWrite     = 0x2,
  kReadWrite = (kRead | kWrite),
};
struct RenderPassBufferReadWriteInfo {
  BufferId buffer_id;
  BufferReadWriteFlag read_write_flag;
  std::byte _pad[3]{};
};
using RenderPassBufferReadWriteInfoListPerPass = vector<RenderPassBufferReadWriteInfo>;
using RenderPassBufferReadWriteInfoList        = vector<RenderPassBufferReadWriteInfoListPerPass>;
enum class RenderFrameLoopSetting :uint8_t { kNoLoop = 0, kWithLoop, };
SignalQueueRenderPassInfo ConfigureQueueSignal(const vector<CommandQueueType>& render_pass_command_queue_type, const RenderPassBufferReadWriteInfoList& pass_buffer_info_list, const RenderFrameLoopSetting loop_type, std::pmr::memory_resource* memory_resource_signal_info, std::pmr::memory_resource* memory_resource_work);
#endif
}
#endif
