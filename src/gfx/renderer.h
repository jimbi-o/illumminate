#ifndef __ILLUMINATE_RENDERER_H__
#define __ILLUMINATE_RENDERER_H__
#include "illuminate.h"
#include <vector>
namespace illuminate::gfx {
enum class BufferFormat : uint8_t {
  kR8G8B8A8_Unorm,
  kD24S8,
  kRgbLinearSdrDefault = kR8G8B8A8_Unorm,
  kDepthBufferDefault = kD24S8,
  kUseSwapchainFormat,
};
enum class BufferSizeType : uint8_t {
  kSwapchainRelative,
  kViewportRelative,
  kAbsolute,
};
struct ClearValue {
  struct DepthStencil { float depth; uint8_t stencil; };
  union {
    float color[4];
    DepthStencil depth_stencil;
  };
};
struct BufferDesc {
  BufferFormat format;
  BufferSizeType size_type;
  float x, y, z;
  ClearValue clear_value;
};
using BufferDescList = std::unordered_map<StrId, BufferDesc>;
enum class QueueType : uint8_t { kGraphics, kCompute, kTransfer, };
enum class AsyncCompute : uint8_t { kDisabled, kEnabled };
enum class BufferState : uint32_t {
  kSrv = 0x0001,
  kRtv = 0x0002,
  kDsv = 0x0004,
  kUav = 0x0008,
};
inline constexpr BufferState operator&(const BufferState a, const BufferState b) { return static_cast<BufferState>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }
inline constexpr BufferState operator|(const BufferState a, const BufferState b) { return static_cast<BufferState>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
enum class BufferLoadOp : uint8_t { kLoad, kClear, kDontCare, };
enum class BufferStoreOp : uint8_t { kStore, kDontCare, };
struct ViewportSize {
  BufferSizeType size_type;
  float width, height;
};
struct PassBindedBuffer {
  StrId buffer_name;
  BufferState state;
  BufferLoadOp  load_op;
  BufferStoreOp store_op;
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
constexpr ClearValue GetClearValueDefaultRtv() {
  return { .color = {0.0f, 0.0f, 0.0f, 1.0f} };
}
constexpr ClearValue GetClearValueDefaultDsv() {
  return { .depth_stencil = {1.0f, 0} };
}
class RendererInterface {
 public:
  virtual ~RendererInterface() {}
};
}
#endif
