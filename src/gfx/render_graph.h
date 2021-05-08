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
enum ReadWriteFlag : uint8_t {
  kReadFlag      = 0x1,
  kWriteFlag     = 0x2,
  kReadWriteFlag = (kReadFlag | kWriteFlag),
};
struct RenderGraphBufferStateConfig {
  StrId buffer_name;
  BufferStateFlags state;
  ReadWriteFlag read_write_flag;
  std::byte _pad[3]{};
};
struct RenderPassConfig {
  StrId pass_name;
  CommandQueueType command_queue_type;
  std::byte _pad[7]{};
};
using RenderPassBufferStateList = vector<vector<RenderGraphBufferStateConfig>>;
class RenderGraphConfig {
 public:
  RenderGraphConfig(std::pmr::memory_resource* memory_resource)
      : memory_resource_(memory_resource)
      , pass_num_(0)
      , render_pass_id_map_(memory_resource_)
      , render_pass_command_queue_type_list_(memory_resource_)
      , render_pass_buffer_state_list_(memory_resource_)
      , initial_buffer_state_list_(memory_resource_)
      , final_buffer_state_list_(memory_resource_)
  {}
  uint32_t CreateNewRenderPass(RenderPassConfig&& render_pass_config) {
    if (render_pass_id_map_.contains(render_pass_config.pass_name)) {
      logerror("render pass name dup. {}", render_pass_config.pass_name);
      return ~0u;
    }
    const auto pass_index = pass_num_;
    render_pass_id_map_.insert_or_assign(render_pass_config.pass_name, pass_index);
    ASSERT(pass_index == render_pass_command_queue_type_list_.size());
    render_pass_command_queue_type_list_.push_back(render_pass_config.command_queue_type);
    ASSERT(render_pass_buffer_state_list_.size() == pass_index);
    render_pass_buffer_state_list_.push_back(vector<RenderGraphBufferStateConfig>{memory_resource_});
    pass_num_++;
    return pass_index;
  }
  void AddNewBufferConfig(const uint32_t pass_index, RenderGraphBufferStateConfig&& config) {
    render_pass_buffer_state_list_[pass_index].push_back(std::move(config));
  }
  void AddBufferInitialState(const StrId& buffer_name, const BufferStateFlags flag) { initial_buffer_state_list_.insert_or_assign(buffer_name, flag); }
  void AddBufferFinalState(const StrId& buffer_name, const BufferStateFlags flag) { final_buffer_state_list_.insert_or_assign(buffer_name, flag); }
  constexpr auto GetRenderPassNum() const { return pass_num_; }
  uint32_t GetRenderPassIndex(const StrId& pass_id) const { return render_pass_id_map_.at(pass_id); }
  constexpr const auto& GetRenderPassCommandQueueTypeList() const { return render_pass_command_queue_type_list_; }
  constexpr const auto& GetRenderPassBufferStateList() const { return render_pass_buffer_state_list_; }
  constexpr const auto& GetBufferInitialStateList() const { return initial_buffer_state_list_; }
  constexpr const auto& GetBufferFinalStateList() const { return final_buffer_state_list_; }
 private:
  std::pmr::memory_resource* memory_resource_;
  uint32_t pass_num_;
  unordered_map<uint32_t, uint32_t> render_pass_id_map_;
  vector<CommandQueueType> render_pass_command_queue_type_list_;
  RenderPassBufferStateList render_pass_buffer_state_list_;
  unordered_map<StrId, BufferStateFlags> initial_buffer_state_list_;
  unordered_map<StrId, BufferStateFlags> final_buffer_state_list_;
  RenderGraphConfig() = delete;
  RenderGraphConfig(const RenderGraphConfig&) = delete;
  void operator=(const RenderGraphConfig&) = delete;
};
enum class DepthStencilFlag : uint8_t { kDefault = 0, kDepthStencilReadOnly, kDepthReadOnly, kStencilReadOnly, };
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
enum class BarrierPosType : uint8_t { kPrePass, kPostPass, };
struct BufferStateChangeInfo {
  uint32_t barrier_begin_pass_index;
  uint32_t barrier_end_pass_index;
  BufferStateFlags state_before;
  BufferStateFlags state_after;
  BarrierPosType barrier_begin_pass_pos_type;
  BarrierPosType barrier_end_pass_pos_type;
  std::byte _pad[2]{};
};
class RenderGraph {
 public:
  RenderGraph(RenderGraphConfig&& config, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work);
  constexpr uint32_t GetRenderPassNum() const { return render_pass_num_; }
  constexpr const auto& GetBufferIdList() const { return buffer_id_list_; }
  constexpr const auto& GetRenderPassBufferIdList() const { return render_pass_buffer_id_list_; }
  constexpr const auto& GetBufferStateChangeInfoListMap() const { return buffer_state_change_info_list_map_; }
  constexpr const auto& GetInterQueuePassDependency() const { return inter_queue_pass_dependency_; }
  constexpr const auto& GetRenderPassCommandQueueTypeList() const { return render_pass_command_queue_type_list_; }
  // TODO create BufferConfig list
 private:
  std::pmr::memory_resource* memory_resource_;
  uint32_t render_pass_num_;
  vector<BufferId> buffer_id_list_;
  vector<vector<BufferId>> render_pass_buffer_id_list_;
  unordered_map<BufferId, vector<uint32_t>> buffer_user_pass_index_;
  unordered_map<BufferId, vector<BufferStateChangeInfo>> buffer_state_change_info_list_map_;
  unordered_map<uint32_t, unordered_set<uint32_t>> inter_queue_pass_dependency_;
  vector<CommandQueueType> render_pass_command_queue_type_list_;
  RenderGraph() = delete;
  RenderGraph(const RenderGraph&) = delete;
  void operator=(const RenderGraph&) = delete;
};
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
using BarrierListPerPass = vector<vector<BarrierConfig>>;
std::tuple<BarrierListPerPass, BarrierListPerPass> ConfigureBarriers(const RenderGraph& render_graph, std::pmr::memory_resource* memory_resource_barriers);
unordered_map<uint32_t, unordered_set<uint32_t>> ConfigureQueueSignals(const RenderGraph& render_graph, std::pmr::memory_resource* memory_resource_signals, std::pmr::memory_resource* memory_resource_work);
#if 0
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
