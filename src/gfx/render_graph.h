#ifndef ILLUMINATE_RENDER_GRAPH_H
#define ILLUMINATE_RENDER_GRAPH_H
#include <cstdint>
#include <optional>
#include <memory_resource>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "gfx_def.h"
#include "core/strid.h"
namespace illuminate::gfx {
enum BufferStateType : uint8_t { kCbv = 0, kSrv, kUav, kRtv, kDsv, kCopySrc, kCopyDst, };
enum BufferLoadOpType : uint8_t { kDontCare = 0, kClear, kLoadWrite, kLoadReadOnly, };
enum BufferDimensionType : uint8_t { kBuffer = 0, k1d, k1dArray, k2d, k2dArray, k3d, k3dArray, kCube, kCubeArray, };
class BufferConfig {
 public:
  constexpr BufferConfig()
      : state_type(BufferStateType::kCbv),
        load_op_type(BufferLoadOpType::kLoadReadOnly),
        format(BufferFormat::kUnknown),
        size_type(BufferSizeType::kMainbufferRelative), width(1.0f), height(1.0f),
        clear_value(GetClearValueDefaultColorBuffer()),
        dimension_type(BufferDimensionType::kBuffer),
        size_type_depth(BufferSizeType::kAbsolute),
        index_to_render(0),
        buffer_num_to_render(1),
        depth(1.0f)
  {}
  constexpr BufferConfig(StrId&& buffer_name, const BufferStateType state)
      : name(std::move(buffer_name)),
        state_type(state),
        load_op_type((state_type == BufferStateType::kSrv || state_type == BufferStateType::kCbv || state_type == BufferStateType::kCopySrc) ? BufferLoadOpType::kLoadReadOnly : (state_type == BufferStateType::kDsv ? BufferLoadOpType::kClear : BufferLoadOpType::kDontCare)),
        format(state_type == BufferStateType::kDsv ? BufferFormat::kD32Float : (state_type == BufferStateType::kCbv ? BufferFormat::kUnknown : BufferFormat::kR8G8B8A8Unorm)),
        size_type(BufferSizeType::kMainbufferRelative), width(1.0f), height(1.0f),
        clear_value(state_type == BufferStateType::kDsv ? GetClearValueDefaultDepthBuffer() : GetClearValueDefaultColorBuffer()),
        dimension_type(state_type == BufferStateType::kCbv ? BufferDimensionType::kBuffer : BufferDimensionType::k2d),
        size_type_depth(BufferSizeType::kAbsolute),
        index_to_render(0),
        buffer_num_to_render(1),
        depth(1.0f)
  {}
  constexpr BufferConfig& LoadOpType(const BufferLoadOpType op) { load_op_type = op; return *this; }
  constexpr BufferConfig& Format(const BufferFormat f) { format = f; return *this; }
  constexpr BufferConfig& Size(const BufferSizeType type, const float w, const float h) { size_type = type; width = w; height = h; return *this; }
  constexpr BufferConfig& ClearValue(ClearValue&& c) { clear_value = std::move(c); return *this; }
  constexpr BufferConfig& SizeDepth(const BufferSizeType type, const float d) { size_type_depth = type; depth = d; return *this; }
  constexpr BufferConfig& Dimension(const BufferDimensionType type) { dimension_type = type; return *this; }
  constexpr BufferConfig& RenderTargetIndex(const uint8_t index, const uint8_t num = 1) { index_to_render = index; buffer_num_to_render = num; return *this; }
  StrId name;
  BufferStateType  state_type;
  BufferLoadOpType load_op_type;
  BufferFormat     format;
  BufferSizeType   size_type;
  float            width;
  float            height;
  illuminate::gfx::ClearValue clear_value;
  BufferDimensionType dimension_type;
  BufferSizeType   size_type_depth;
  uint8_t          index_to_render;
  uint8_t          buffer_num_to_render;
  float            depth;
};
using BufferConfigList = std::pmr::vector<BufferConfig>;
class RenderPass {
 public:
  RenderPass()
      : mandatory_pass(false)
  {}
  RenderPass(StrId&& buffer_name, BufferConfigList&& buffer_config_list)
      : name(std::move(buffer_name)),
        buffer_list(std::move(buffer_config_list)),
        mandatory_pass(false)
  {}
  constexpr RenderPass& Mandatory(const bool b) { mandatory_pass = b; return *this; }
  StrId name;
  BufferConfigList buffer_list;
  bool mandatory_pass;
  std::byte _pad[7];
};
using RenderPassList = std::pmr::vector<RenderPass>;
using RenderPassIdMap = std::pmr::unordered_map<StrId, RenderPass>;
using RenderPassOrder = std::pmr::vector<StrId>;
std::tuple<RenderPassIdMap, RenderPassOrder> FormatRenderPassList(RenderPassList&& render_pass_list, std::pmr::memory_resource* memory_resource);
using BufferIdList = std::pmr::unordered_map<StrId, std::pmr::vector<uint32_t>>;
BufferIdList CreateBufferIdMap(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, std::pmr::memory_resource* memory_resource);
using BufferNameAliasList = std::pmr::unordered_map<StrId, StrId>;
BufferIdList ApplyBufferNameAlias(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, BufferIdList&& buffer_id_list, const BufferNameAliasList& alias_list, std::pmr::memory_resource* memory_resource);
}
#endif