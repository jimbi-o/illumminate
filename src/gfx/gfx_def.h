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
  kMainbufferRelative,
  kSwapchainRelative,
  kAbsolute,
};
constexpr uint32_t GetPhysicalBufferSize(const BufferSizeType size_type, const float val, const uint32_t mainbuffer, const uint32_t swapchain) {
  switch (size_type) {
    case BufferSizeType::kSwapchainRelative:  return static_cast<uint32_t>(val * static_cast<float>(swapchain));
    case BufferSizeType::kMainbufferRelative: return static_cast<uint32_t>(val * static_cast<float>(mainbuffer));
    case BufferSizeType::kAbsolute: return static_cast<uint32_t>(val);
  }
}
struct ClearValueDepthStencil { float depth; uint8_t stencil; uint8_t _dmy[3]; };
using ClearValue = std::variant<std::array<float, 4>, ClearValueDepthStencil>;
struct BufferDesc {
  BufferFormat format;
  BufferSizeType size_type;
  float x, y, z;
  ClearValue clear_value;
};
enum class CommandQueueType : uint8_t { kGraphics = 0, kCompute, kTransfer, kNum, kInvalid = kNum, };
static const CommandQueueType kCommandQueueTypeSet[]{CommandQueueType::kGraphics, CommandQueueType::kCompute, CommandQueueType::kTransfer};
static const auto kCommandQueueTypeNum = static_cast<uint32_t>(CommandQueueType::kNum);
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
