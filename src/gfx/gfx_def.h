#ifndef ILLUMINATE_GFX_DEF_H
#define ILLUMINATE_GFX_DEF_H
#include <array>
#include <cstdint>
#include <unordered_set>
#include <variant>
namespace illuminate::gfx {
enum class BufferFormat : uint8_t {
  kUnknown = 0,
  kR8G8B8A8Unorm,
  kD24S8,
  kD32Float,
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
  return static_cast<uint32_t>(size);
}
template <typename T>
struct Size2d {
  T x, y;
};
struct ClearValueDepthStencil { float depth; uint8_t stencil; uint8_t _dmy[3]; };
using ClearValue = std::variant<std::array<float, 4>, ClearValueDepthStencil>;
struct BufferDesc {
  BufferFormat format;
  BufferSizeType size_type;
  float x, y, z;
  ClearValue clear_value;
};
using Size2dUint = Size2d<uint32_t>;
constexpr Size2dUint GetPhysicalSize(const BufferDesc& desc, const Size2dUint& swapchain_size, const Size2dUint& mainbuffer_size) {
  return {
    GetPhysicalSize(desc.size_type, desc.x, swapchain_size.x, mainbuffer_size.x),
    GetPhysicalSize(desc.size_type, desc.y, swapchain_size.y, mainbuffer_size.y),
  };
}
enum class CommandQueueType : uint8_t { kGraphics, kCompute, kTransfer, };
static const CommandQueueType kCommandQueueTypeSet[]{CommandQueueType::kGraphics, CommandQueueType::kCompute, CommandQueueType::kTransfer};
constexpr auto GetClearValueDefaultColorBuffer() {
  return ClearValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
}
constexpr auto GetClearValueDefaultDepthBuffer() {
  return ClearValue(ClearValueDepthStencil{1.0f, 0, {}});
}
constexpr auto GetClearValueColorBuffer(const ClearValue& clear_value) {
  return std::get<0>(clear_value);
}
constexpr auto GetClearValueDepthBuffer(const ClearValue& clear_value) {
  return std::get<1>(clear_value);
}
enum BufferStateType : uint8_t { kCbv = 0, kSrv, kUav, kRtv, kDsv, kCopySrc, kCopyDst, };
enum BufferLoadOpType : uint8_t { kDontCare = 0, kClear, kLoadWrite, kLoadReadOnly, };
enum BufferDimensionType : uint8_t { kBuffer = 0, k1d, k1dArray, k2d, k2dArray, k3d, k3dArray, kCube, kCubeArray, };
}
#endif
