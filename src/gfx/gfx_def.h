#ifndef __ILLUMINATE_GFX_DEF_H__
#define __ILLUMINATE_GFX_DEF_H__
#include <cstdint>
#include <unordered_set>
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
  kMainbufferRelative,
  kAbsolute,
};
constexpr uint32_t GetPhysicalSize(const BufferSizeType type, const float size, const uint32_t swapchain_size, const uint32_t mainbuffer_size) {
  switch (type) {
    case BufferSizeType::kSwapchainRelative:  return static_cast<uint32_t>(static_cast<float>(swapchain_size)  * size);
    case BufferSizeType::kMainbufferRelative: return static_cast<uint32_t>(static_cast<float>(mainbuffer_size) * size);
    case BufferSizeType::kAbsolute:           return static_cast<uint32_t>(size);
  }
  return size;
}
template <typename T>
struct Size2d {
  T x, y;
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
using Size2dUint = Size2d<uint32_t>;
constexpr Size2dUint GetPhysicalSize(const BufferDesc& desc, const Size2dUint& swapchain_size, const Size2dUint& mainbuffer_size) {
  return {
    GetPhysicalSize(desc.size_type, desc.x, swapchain_size.x, mainbuffer_size.x),
    GetPhysicalSize(desc.size_type, desc.y, swapchain_size.y, mainbuffer_size.y),
  };
}
enum class CommandQueueType : uint8_t { kGraphics, kCompute, kTransfer, };
const std::unordered_set<CommandQueueType> kCommandQueueTypeSet{CommandQueueType::kGraphics, CommandQueueType::kCompute, CommandQueueType::kTransfer};
constexpr ClearValue GetClearValueDefaultRtv() {
  return { .color = {0.0f, 0.0f, 0.0f, 1.0f} };
}
constexpr ClearValue GetClearValueDefaultDsv() {
  return { .depth_stencil = {1.0f, 0} };
}
}
#endif
