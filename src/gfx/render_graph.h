#ifndef ILLUMINATE_RENDER_GRAPH_H
#define ILLUMINATE_RENDER_GRAPH_H
#include <cstdint>
#include <optional>
#include <memory_resource>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "gfx_def.h"
#include "illuminate.h"
namespace illuminate::gfx {
constexpr bool IsOutputBuffer(const BufferStateType state_type, const BufferLoadOpType load_op_type) {
  switch (state_type) {
    case kCbv: return false;
    case kSrv: return false;
    case kUav: return (load_op_type == kLoadReadOnly) ? false : true;
    case kRtv: return true;
    case kDsv: return (load_op_type == kLoadReadOnly) ? false : true;
    case kCopySrc: return false;
    case kCopyDst: return true;
  }
}
constexpr bool IsInitialValueUsed(const BufferStateType state_type, const BufferLoadOpType load_op_type) {
  switch (state_type) {
    case kCbv: return true;
    case kSrv: return true;
    case kUav: return (load_op_type == kDontCare || load_op_type == kClear) ? false : true;
    case kRtv: return (load_op_type == kDontCare || load_op_type == kClear) ? false : true;
    case kDsv: return (load_op_type == kDontCare || load_op_type == kClear) ? false : true;
    case kCopySrc: return true;
    case kCopyDst: return false;
  }
}
class BufferConfig {
 public:
  constexpr BufferConfig()
      : state_type(BufferStateType::kCbv),
        load_op_type(BufferLoadOpType::kLoadReadOnly),
        format(BufferFormat::kUnknown),
        size_type(BufferSizeType::kMainbufferRelative), width(1.0f), height(1.0f),
        clear_value(GetClearValueDefaultColorBuffer()),
        dimension_type(BufferDimensionType::kBuffer),
        index_to_render(0),
        buffer_num_to_render(1),
        depth(1)
  {}
  constexpr BufferConfig(StrId&& buffer_name, const BufferStateType state)
      : name(std::move(buffer_name)),
        state_type(state),
        load_op_type((state_type == BufferStateType::kSrv || state_type == BufferStateType::kCbv || state_type == BufferStateType::kCopySrc) ? BufferLoadOpType::kLoadReadOnly : (state_type == BufferStateType::kDsv ? BufferLoadOpType::kClear : BufferLoadOpType::kDontCare)),
        format(state_type == BufferStateType::kDsv ? BufferFormat::kD32Float : (state_type == BufferStateType::kCbv ? BufferFormat::kUnknown : BufferFormat::kR8G8B8A8Unorm)),
        size_type(BufferSizeType::kMainbufferRelative), width(1.0f), height(1.0f),
        clear_value(state_type == BufferStateType::kDsv ? GetClearValueDefaultDepthBuffer() : GetClearValueDefaultColorBuffer()),
        dimension_type(state_type == BufferStateType::kCbv ? BufferDimensionType::kBuffer : BufferDimensionType::k2d),
        index_to_render(0),
        buffer_num_to_render(1),
        depth(1)
  {}
  constexpr BufferConfig& LoadOpType(const BufferLoadOpType op) { load_op_type = op; return *this; }
  constexpr BufferConfig& Format(const BufferFormat f) { format = f; return *this; }
  constexpr BufferConfig& Size(const BufferSizeType type, const float w, const float h) { size_type = type; width = w; height = h; return *this; }
  constexpr BufferConfig& ClearValue(ClearValue&& c) { clear_value = std::move(c); return *this; }
  constexpr BufferConfig& Depth(const uint32_t d) { depth = d; return *this; }
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
  uint8_t          index_to_render;
  uint8_t          buffer_num_to_render;
  std::byte        _pad;
  uint32_t         depth;
};
struct BufferSize2d { uint32_t width, height; };
constexpr uint32_t GetPhysicalBufferWidth(const BufferConfig& config, const BufferSize2d& mainbuffer, const BufferSize2d& swapchain) {
  return GetPhysicalBufferSize(config.size_type, config.width, mainbuffer.width, swapchain.width);
}
constexpr uint32_t GetPhysicalBufferHeight(const BufferConfig& config, const BufferSize2d& mainbuffer, const BufferSize2d& swapchain) {
  return GetPhysicalBufferSize(config.size_type, config.height, mainbuffer.height, swapchain.height);
}
using BufferConfigList = std::pmr::vector<BufferConfig>;
using AsyncComputeEnabled = illuminate::EnableDisable;
class RenderPass {
 public:
  RenderPass()
      : command_queue_type(CommandQueueType::kGraphics),
        mandatory_pass(false),
        async_compute_enabled(AsyncComputeEnabled::kDisabled)
  {}
  RenderPass(StrId&& pass_name, BufferConfigList&& buffer_config_list)
      : name(std::move(pass_name)),
        buffer_list(std::move(buffer_config_list)),
        command_queue_type(CommandQueueType::kGraphics),
        mandatory_pass(false),
        async_compute_enabled(AsyncComputeEnabled::kDisabled)
  {}
  constexpr RenderPass& CommandQueueTypeGraphics() { command_queue_type = CommandQueueType::kGraphics; return *this; }
  constexpr RenderPass& CommandQueueTypeCompute() { command_queue_type = CommandQueueType::kCompute; return *this; }
  constexpr RenderPass& CommandQueueTypeTransfer() { command_queue_type = CommandQueueType::kTransfer; return *this; }
  constexpr RenderPass& Mandatory(const bool b = true) { mandatory_pass = b; return *this; }
  constexpr RenderPass& AsyncComputeGroup(StrId&& group_name) { async_compute_group = std::move(group_name); return EnableAsyncCompute(); }
  constexpr RenderPass& EnableAsyncCompute() { async_compute_enabled = AsyncComputeEnabled::kEnabled; return *this; }
  StrId name;
  BufferConfigList buffer_list;
  CommandQueueType command_queue_type;
  bool mandatory_pass;
  AsyncComputeEnabled async_compute_enabled;
  std::byte _pad[5];
  StrId async_compute_group;
};
using RenderPassList = std::pmr::vector<RenderPass>;
using RenderPassIdMap = std::pmr::unordered_map<StrId, RenderPass>;
using RenderPassOrder = std::pmr::vector<StrId>;
std::tuple<RenderPassIdMap, RenderPassOrder> FormatRenderPassList(RenderPassList&& render_pass_list, std::pmr::memory_resource* memory_resource);
using BufferId = uint32_t;
using PassBufferIdList = std::pmr::vector<BufferId>;
using BufferIdList = std::pmr::unordered_map<StrId, PassBufferIdList>;
BufferIdList CreateBufferIdList(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, std::pmr::memory_resource* memory_resource);
using BufferNameAliasList = std::pmr::unordered_map<StrId, StrId>;
BufferIdList ApplyBufferNameAlias(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, BufferIdList&& buffer_id_list, const BufferNameAliasList& alias_list, std::pmr::memory_resource* memory_resource);
class RenderPassAdjacencyGraph {
 public:
  RenderPassAdjacencyGraph(std::pmr::memory_resource* memory_resource) : output_buffer_producer_pass(memory_resource), consumer_pass_input_buffer(memory_resource) {}
  std::pmr::unordered_map<BufferId, std::pmr::vector<StrId>> output_buffer_producer_pass;
  std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>> consumer_pass_input_buffer;
};
RenderPassAdjacencyGraph CreateRenderPassAdjacencyGraph(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, std::pmr::memory_resource* memory_resource);
using MandatoryOutputBufferNameList = std::pmr::vector<StrId>;
using MandatoryOutputBufferIdList = std::pmr::unordered_map<StrId, BufferId>;
MandatoryOutputBufferIdList IdentifyMandatoryOutputBufferId(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, const MandatoryOutputBufferNameList& mandatory_buffer_name_list, std::pmr::memory_resource* memory_resource);
std::pmr::unordered_set<StrId> GetBufferProducerPassList(const RenderPassAdjacencyGraph& adjacency_graph, std::pmr::unordered_set<BufferId>&& buffer_id_list, std::pmr::memory_resource* memory_resource);
using ConsumerProducerRenderPassMap = std::pmr::unordered_map<StrId, std::pmr::unordered_set<StrId>>;
ConsumerProducerRenderPassMap CreateConsumerProducerMap(const RenderPassAdjacencyGraph& adjacency_graph, std::pmr::memory_resource* memory_resource);
std::pmr::unordered_set<StrId> GetUsedRenderPassList(std::pmr::unordered_set<StrId>&& used_pass, const ConsumerProducerRenderPassMap& consumer_producer_render_pass_map);
RenderPassOrder CullUnusedRenderPass(RenderPassOrder&& render_pass_order, const std::pmr::unordered_set<StrId>& used_render_pass_list, const RenderPassIdMap& render_pass_id_map);
bool IsDuplicateRenderPassNameExists(const RenderPassList& list, std::pmr::memory_resource* memory_resource);
enum BufferStateFlags : uint32_t {
  kBufferStateFlagNone      = 0x0000,
  kBufferStateFlagCbv       = 0x0001,
  kBufferStateFlagSrvPsOnly = 0x0002,
  kBufferStateFlagSrvNonPs  = 0x0004,
  kBufferStateFlagUav       = 0x0008,
  kBufferStateFlagRtv       = 0x0010,
  kBufferStateFlagDsvWrite  = 0x0020,
  kBufferStateFlagDsvRead   = 0x0040,
  kBufferStateFlagCopySrc   = 0x0080,
  kBufferStateFlagCopyDst   = 0x0100,
  kBufferStateFlagPresent   = 0x0200,
  kBufferStateFlagSrv       = kBufferStateFlagSrvPsOnly | kBufferStateFlagSrvNonPs,
};
constexpr BufferStateFlags GetBufferStateFlag(const BufferStateType type, const BufferLoadOpType load_op_type) {
  switch (type) {
    case kCbv: return kBufferStateFlagCbv;
    case kSrv: return kBufferStateFlagSrv;
    case kUav: return kBufferStateFlagUav;
    case kRtv: return kBufferStateFlagRtv;
    case kDsv: return (load_op_type == BufferLoadOpType::kLoadReadOnly) ? kBufferStateFlagDsvRead : kBufferStateFlagDsvWrite;
    case kCopySrc: return kBufferStateFlagCopySrc;
    case kCopyDst: return kBufferStateFlagCopyDst;
  }
}
constexpr bool IsWritableState(const BufferStateFlags state) {
  if (state & kBufferStateFlagUav) return true;
  if (state & kBufferStateFlagRtv) return true;
  if (state & kBufferStateFlagDsvWrite) return true;
  if (state & kBufferStateFlagCopyDst) return true;
  return false;
}
constexpr bool IsBufferStateFlagMergeable(const BufferStateFlags a, const BufferStateFlags b) {
  if (IsWritableState(a)) return false;
  if (IsWritableState(b)) return false;
  return true;
}
class BufferCreationDesc {
 public:
  BufferCreationDesc()
      : format(BufferFormat::kUnknown),
        dimension_type(BufferDimensionType::k2d),
        initial_state_flag(kBufferStateFlagNone),
        state_flags(kBufferStateFlagNone),
        width(0),
        height(0),
        depth(1),
        clear_value({})
  {}
  BufferCreationDesc(const BufferConfig& config, const BufferSize2d& mainbuffer, const BufferSize2d& swapchain)
      : format(config.format),
        dimension_type(config.dimension_type),
        initial_state_flag(GetBufferStateFlag(config.state_type, config.load_op_type)),
        state_flags(initial_state_flag),
        width(GetPhysicalBufferWidth(config, mainbuffer, swapchain)),
        height(GetPhysicalBufferHeight(config, mainbuffer, swapchain)),
        depth(config.depth),
        clear_value(config.clear_value) // TODO consider using move.
  {}
  BufferFormat format;
  BufferDimensionType dimension_type;
  std::byte _pad[2];
  BufferStateFlags initial_state_flag;
  BufferStateFlags state_flags;
  uint32_t width, height, depth;
  ClearValue clear_value;
};
using BufferCreationDescList = std::pmr::unordered_map<BufferId, BufferCreationDesc>;
BufferCreationDescList ConfigureBufferCreationDescs(const RenderPassOrder& render_pass_order, const RenderPassIdMap& render_pass_id_map, const BufferIdList& buffer_id_list, const BufferSize2d& mainbuffer_size, const BufferSize2d& swapchain_size, std::pmr::memory_resource* memory_resource);
std::tuple<std::pmr::unordered_map<BufferId, uint32_t>, std::pmr::unordered_map<BufferId, uint32_t>> GetPhysicalBufferSizes(const BufferCreationDescList& buffer_creation_descs, std::function<std::tuple<uint32_t, uint32_t>(const BufferCreationDesc&)>&& buffer_creation_func, std::pmr::memory_resource* memory_resource);
std::tuple<std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>, std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>> CalculatePhysicalBufferLiftime(const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, std::pmr::memory_resource* memory_resource);
std::pmr::unordered_map<BufferId, uint32_t> GetPhysicalBufferAddressOffset(const RenderPassOrder& render_pass_order, const std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>& physical_buffer_lifetime_begin_pass, const std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>& physical_buffer_lifetime_end_pass, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_size_in_byte, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_alignment, std::pmr::memory_resource* memory_resource);
using BatchInfoList = std::pmr::vector<std::pmr::vector<StrId>>;
enum class AsyncComputeBatchPairType : uint8_t { kCurrentFrame = 0, kPairComputeWithNextFrameGraphics, };
using AsyncComputePairInfo = std::pmr::unordered_map<StrId, AsyncComputeBatchPairType>;
std::tuple<BatchInfoList, RenderPassOrder> ConfigureAsyncComputeBatching(const RenderPassIdMap& render_pass_id_map, RenderPassOrder&& current_render_pass_order, RenderPassOrder&& prev_render_pass_order, const AsyncComputePairInfo& async_group_info, std::pmr::memory_resource* memory_resource);
using ProducerPassSignalList = std::pmr::unordered_map<StrId, uint32_t>;
using ConsumerPassWaitingSignalList = std::pmr::unordered_map<StrId, StrId>;
std::tuple<ProducerPassSignalList, ConsumerPassWaitingSignalList> ConfigureBufferResourceDependency(const RenderPassIdMap& render_pass_id_map, const BatchInfoList& src_batch, const ConsumerProducerRenderPassMap& consumer_producer_render_pass_map, std::pmr::memory_resource* memory_resource);
// barrier
using BufferStateList = std::pmr::unordered_map<uint32_t, BufferStateFlags>;
enum class BarrierSplitType : uint8_t { kNone = 0, kBegin, kEnd, };
struct BarrierConfig {
  BufferId buffer_id;
  BufferStateFlags state_flag_before_pass;
  BufferStateFlags state_flag_after_pass;
  BarrierSplitType split_type;
  std::byte _pad[3]{};
};
using PassBarrierInfo = std::pmr::unordered_map<StrId, std::pmr::vector<BarrierConfig>>;
struct PassBarrierInfoSet {
  PassBarrierInfo barrier_before_pass;
  PassBarrierInfo barrier_after_pass;
};
using PassSignalInfo = std::pmr::unordered_map<StrId, std::pmr::unordered_set<StrId>>;
PassSignalInfo ConvertBatchToSignalInfo(const BatchInfoList& batch_info_list, const RenderPassIdMap& render_pass_id_map, std::pmr::memory_resource* memory_resource);
RenderPassOrder ConvertBatchInfoBackToRenderPassOrder(BatchInfoList&& batch_info_list, std::pmr::memory_resource* memory_resource);
PassBarrierInfoSet ConfigureBarrier(const RenderPassOrder& render_pass_order, const RenderPassIdMap& render_pass_id_map, const PassSignalInfo& pass_signal_info, const BufferIdList& buffer_id_list, const BufferStateList& buffer_state_before_render_pass_list, const BufferStateList& buffer_state_after_render_pass_list, std::pmr::memory_resource* memory_resource);
}
#endif
