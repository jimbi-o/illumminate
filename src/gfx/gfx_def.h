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
enum class CommandQueueType : uint8_t { kGraphics = 0, kCompute, kTransfer, };
static const CommandQueueType kCommandQueueTypeSet[]{CommandQueueType::kGraphics, CommandQueueType::kCompute, CommandQueueType::kTransfer};
static const auto kCommandQueueTypeNum = static_cast<uint32_t>(CommandQueueType::kTransfer) + 1;
enum CommandQueueTypeFlag : uint8_t {
  kCommandQueueTypeNone     = 0x0,
  kCommandQueueTypeGraphics = 0x1,
  kCommandQueueTypeCompute  = 0x2,
  kCommandQueueTypeTransfer = 0x4,
  kCommandQueueTypeAll      = (0x4 | 0x2 | 0x1),
  kCommandQueueTypeGC       = (0x2 | 0x1),
};
constexpr CommandQueueTypeFlag ConvertCommandQueueTypeToFlag(const CommandQueueType& type) {
  switch (type) {
    case CommandQueueType::kGraphics: return kCommandQueueTypeGraphics;
    case CommandQueueType::kCompute:  return kCommandQueueTypeCompute;
    case CommandQueueType::kTransfer: return kCommandQueueTypeTransfer;
  }
}
constexpr bool IsContainingCommandQueueType(const int32_t& flag, const CommandQueueType& type) {
  return flag & ConvertCommandQueueTypeToFlag(type);
}
constexpr void MergeCommandQueueTypeFlag(CommandQueueTypeFlag* const a, const CommandQueueType& b) {
  *a = static_cast<CommandQueueTypeFlag>(*a | ConvertCommandQueueTypeToFlag(b));
}
constexpr auto CreateClearValueColorBuffer(float&& r, float&& g, float&& b, float&& a) {
  return ClearValue(std::array<float, 4>{std::move(r), std::move(g), std::move(b), std::move(a)});
}
constexpr auto GetClearValueDefaultColorBuffer() {
  return CreateClearValueColorBuffer(0.0f, 0.0f, 0.0f, 1.0f);
}
constexpr auto CreateClearValueColorBuffer(float&& depth, uint8_t&& stencil) {
  return ClearValue(ClearValueDepthStencil{std::move(depth), std::move(stencil), {}});
}
constexpr auto GetClearValueDefaultDepthBuffer() {
  return CreateClearValueColorBuffer(1.0f, 0);
}
constexpr auto&& GetClearValueColorBuffer(const ClearValue& clear_value) {
  return std::get<0>(clear_value);
}
constexpr auto&& GetClearValueDepthBuffer(const ClearValue& clear_value) {
  return std::get<1>(clear_value);
}
enum BufferStateType : uint8_t { kCommon = 0, kCbv, kSrvPsOnly, kSrvNonPs, kSrvAll, kUav, kRtv, kDsv, kCopySrc, kCopyDst, };
enum BufferLoadOpType : uint8_t { kDontCare = 0, kClear, kLoadWrite, kLoadReadOnly, };
enum BufferDimensionType : uint8_t { kBuffer = 0, k1d, k1dArray, k2d, k2dArray, k3d, kCube, kCubeArray, kAS, };
enum ShaderType : uint8_t { kPs, kVs, kGs, kHs, kDs, kCs, kLib, kMs, kAs, };
}
#endif
