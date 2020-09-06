#ifndef __ILLUMINATE_RENDERER_H__
#define __ILLUMINATE_RENDERER_H__
#include "illuminate.h"
#include <type_traits>
#include <vector>
#include "gfx_def.h"
namespace illuminate::gfx {
enum class AsyncCompute : uint8_t { kDisabled, kEnabled };
using BufferViewType = uint16_t;
static const BufferViewType kBufferViewTypeSrv = 0x01;
static const BufferViewType kBufferViewTypeRtv = 0x02;
static const BufferViewType kBufferViewTypeDsv = 0x04;
static const BufferViewType kBufferViewTypeUav = 0x08;
using BufferLoadOp = uint8_t;
static const BufferLoadOp kBufferLoadOpLoad     = 0x0;
static const BufferLoadOp kBufferLoadOpClear    = 0x1;
static const BufferLoadOp kBufferLoadOpDontCare = 0x2;
using BufferStoreOp = uint8_t;
static const BufferStoreOp kBufferStoreOpStore    = 0x0;
static const BufferStoreOp kBufferStoreOpDontCare = 0x1;
constexpr inline uint32_t CombineBufferFlags(const BufferViewType view_type, const uint8_t load_op, const uint8_t store_op) { return view_type | (load_op << 16) | (store_op << 24); }
enum class BufferState : uint32_t {
  kSrvLoadDontCare  = CombineBufferFlags(kBufferViewTypeSrv, kBufferLoadOpLoad,     kBufferStoreOpDontCare),
  kSrv              = kSrvLoadDontCare,
  kRtvDontCareStore = CombineBufferFlags(kBufferViewTypeRtv, kBufferLoadOpDontCare, kBufferStoreOpStore),
  kRtvClearStore    = CombineBufferFlags(kBufferViewTypeRtv, kBufferLoadOpClear,    kBufferStoreOpStore),
  kRtvLoadStore     = CombineBufferFlags(kBufferViewTypeRtv, kBufferLoadOpLoad,     kBufferStoreOpStore),
  kRtv              = kRtvDontCareStore,
  kRtvBlendable     = kRtvLoadStore,
  kDsvDontCareStore = CombineBufferFlags(kBufferViewTypeDsv, kBufferLoadOpDontCare, kBufferStoreOpStore),
  kDsvClearStore    = CombineBufferFlags(kBufferViewTypeDsv, kBufferLoadOpClear,    kBufferStoreOpStore),
  kDsvLoadStore     = CombineBufferFlags(kBufferViewTypeDsv, kBufferLoadOpLoad,     kBufferStoreOpStore),
  kDsvLoadDontCare  = CombineBufferFlags(kBufferViewTypeDsv, kBufferLoadOpLoad,     kBufferStoreOpDontCare),
  kDsv              = kDsvClearStore,
  kDsvReadOnly      = kDsvLoadDontCare,
  kUavDontCareStore = CombineBufferFlags(kBufferViewTypeUav, kBufferLoadOpDontCare, kBufferStoreOpStore),
  kUavClearStore    = CombineBufferFlags(kBufferViewTypeUav, kBufferLoadOpClear,    kBufferStoreOpStore),
  kUavLoadStore     = CombineBufferFlags(kBufferViewTypeUav, kBufferLoadOpLoad,     kBufferStoreOpStore),
  kUavLoadDontCare  = CombineBufferFlags(kBufferViewTypeUav, kBufferLoadOpLoad,     kBufferStoreOpDontCare),
  kUav              = kUavDontCareStore,
  kUavReadOnly      = kUavLoadDontCare,
  kInvalid          = 0xFFFFFFFF,
};
constexpr inline BufferViewType GetBufferViewType(const BufferState state) { return static_cast<std::underlying_type_t<BufferState>>(state) & 0x0000FFFF; }
constexpr inline bool IsBufferLoadOpLoad(const BufferState state) { return (static_cast<std::underlying_type_t<BufferState>>(state) & 0x00FF0000) == 0x00000000; }
constexpr inline bool IsBufferStoreOpStore(const BufferState state) { return (static_cast<std::underlying_type_t<BufferState>>(state) & 0xFF000000) == 0x00000000; }
constexpr inline bool IsBufferStateReadOnly(const BufferState state) { return IsBufferLoadOpLoad(state) && !IsBufferStoreOpStore(state); }
struct ViewportSize {
  BufferSizeType size_type;
  float width, height;
};
struct PassBindedBuffer {
  StrId buffer_name;
  BufferState state;
};
using PassBindedBufferList = std::vector<PassBindedBuffer>;
struct RenderPassConfig {
  StrId pass_name;
  QueueType queue_type;
  AsyncCompute async_compute_enabled;
  StrId render_function_name;
  ViewportSize viewport_size;
  PassBindedBufferList pass_binded_buffers;
};
using RenderPassConfigList = std::vector<RenderPassConfig>;
using BatchLocalBufferDescList = std::unordered_map<StrId, BufferDesc>;
struct BatchedRendererPass {
  StrId batch_name;
  RenderPassConfigList pass_configs;
  BatchLocalBufferDescList batch_local_buffer_descs;
};
enum class StateTransitionSplitFlag : uint8_t { kNone, kBegin, kEnd, };
struct StateTransitionDesc {
  BufferState state_before;
  BufferState state_after;
  StateTransitionSplitFlag split_flag;
};
using StateTransitionList = std::vector<StateTransitionDesc>;
struct BufferDescImpl {
  uint32_t width, height;
  BufferFormat format;
  BufferViewType transitioned_viewtypes;
  BufferState initial_state;
  ClearValue clear_value;
  StateTransitionList state_transition_list;
};
class RendererInterface {
 public:
  virtual ~RendererInterface() {}
};
}
#endif
