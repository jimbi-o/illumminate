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
struct BufferDesc {
  BufferFormat format;
  BufferSizeType size_type;
  float x, y, z;
};
using BufferDescList = std::unordered_map<StrId, BufferDesc>;
enum class QueueType : uint8_t { kGraphics, kCompute };
enum class AsyncCompute : uint8_t { kDisable, kEnable };
enum class BufferRWType : uint8_t {
  kReadSrv,
  kReadDsv,
  kReadUav,
  kWriteRtv,
  kWriteDsv,
  kWriteUav,
};
struct ViewportSize {
  BufferSizeType size_type;
  float width, height;
};
struct PassBindedBuffer {
  StrId buffer_name;
  BufferRWType rw_type;
};
using PassBindedBufferList = std::vector<PassBindedBuffer>;
struct ClearValue {
  struct DepthStencil { float depth; uint8_t stencil; };
  union {
    float color[4];
    DepthStencil depth_stencil;
  };
};
using ClearRequiredBufferList = std::unordered_map<StrId, ClearValue>;
struct RenderPass {
  StrId pass_name;
  QueueType queue_type;
  AsyncCompute async_compute_enabled;
  StrId render_function_name;
  ViewportSize viewport_size;
  PassBindedBufferList pass_binded_buffers;
  ClearRequiredBufferList clear_required_buffers;
};
using RenderPassList = std::vector<RenderPass>;
using BatchLocalBufferDescList = std::unordered_map<StrId, BufferDesc>;
struct BatchedRendererPass {
  StrId batch_name;
  RenderPassList pass_list;
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
  virtual void ExecuteBatchedRendererPass(const BatchedRendererPass* const batch_list, const uint32_t batch_num, const BufferDescList& global_buffer_descs) = 0;
};
extern RendererInterface* CreateRendererD3d12();
}
#endif
