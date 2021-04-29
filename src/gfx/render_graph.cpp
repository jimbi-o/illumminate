#include "render_graph.h"
namespace illuminate::gfx {
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
using RenderPassBufferStateList = vector<vector<RenderGraphBufferStateConfig>>;
class RenderGraphConfig {
 public:
  RenderGraphConfig(std::pmr::memory_resource* memory_resource)
      : memory_resource_(memory_resource)
      , pass_index_used_(0)
      , render_pass_buffer_state_list_(memory_resource_)
      , initial_buffer_state_list_(memory_resource_)
      , final_buffer_state_list_(memory_resource_)
  {}
  uint32_t CreateNewRenderPass() {
    auto pass_index = pass_index_used_;
    ASSERT(render_pass_buffer_state_list_.size() == pass_index);
    render_pass_buffer_state_list_.push_back(vector<RenderGraphBufferStateConfig>{memory_resource_});
    pass_index_used_++;
    return pass_index;
  }
  void AddNewBufferConfig(const uint32_t pass_index, RenderGraphBufferStateConfig&& config)  {
    render_pass_buffer_state_list_[pass_index].push_back(std::move(config));
  }
  void AddBufferInitialState(const StrId& buffer_name, const BufferStateFlags flag) { initial_buffer_state_list_.insert_or_assign(buffer_name, flag); }
  void AddBufferFinalState(const StrId& buffer_name, const BufferStateFlags flag) { final_buffer_state_list_.insert_or_assign(buffer_name, flag); }
  constexpr auto GetRenderPassNum() const { return pass_index_used_; }
  constexpr const auto& GetRenderPassBufferStateList() const { return render_pass_buffer_state_list_; }
  constexpr const auto& GetBufferInitialStateList() const { return initial_buffer_state_list_; }
  constexpr const auto& GetBufferFinalStateList() const { return final_buffer_state_list_; }
 private:
  std::pmr::memory_resource* memory_resource_;
  uint32_t pass_index_used_;
  RenderPassBufferStateList render_pass_buffer_state_list_;
  unordered_map<StrId, BufferStateFlags> initial_buffer_state_list_;
  unordered_map<StrId, BufferStateFlags> final_buffer_state_list_;
  RenderGraphConfig() = delete;
  RenderGraphConfig(const RenderGraphConfig&) = delete;
  void operator=(const RenderGraphConfig&) = delete;
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
static std::tuple<vector<BufferId>, vector<vector<BufferId>>> InitBufferIdList(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, std::pmr::memory_resource* memory_resource) {
  vector<BufferId> buffer_id_list{memory_resource};
  vector<vector<BufferId>> render_pass_buffer_id_list{memory_resource};
  render_pass_buffer_id_list.reserve(render_pass_buffer_state_list.size());
  unordered_map<StrId, BufferId> used_buffer_name{memory_resource};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& buffer_state_list = render_pass_buffer_state_list[pass_index];
    render_pass_buffer_id_list.push_back(vector<BufferId>{memory_resource});
    render_pass_buffer_id_list.back().reserve(buffer_state_list.size());
    for (auto& buffer_state : buffer_state_list) {
      if (used_buffer_name.contains(buffer_state.buffer_name) && (buffer_state.read_write_flag & kReadFlag)) {
        render_pass_buffer_id_list.back().push_back(used_buffer_name.at(buffer_state.buffer_name));
        continue;
      }
      BufferId buffer_id = static_cast<BufferId>(used_buffer_name.size());
      used_buffer_name.insert_or_assign(buffer_state.buffer_name, buffer_id);
      buffer_id_list.push_back(buffer_id);
      render_pass_buffer_id_list.back().push_back(buffer_id);
    }
  }
  return {buffer_id_list, render_pass_buffer_id_list};
}
static unordered_map<BufferId, BufferStateFlags> ConfigureInitialBufferStateFlags(const unordered_map<StrId, BufferStateFlags>& initial_buffer_state_list_with_buffer_name, const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, const vector<BufferId>& buffer_id_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  const auto buffer_num = static_cast<uint32_t>(buffer_id_list.size());
  unordered_map<BufferId, BufferStateFlags> initial_buffer_state_list{memory_resource};
  initial_buffer_state_list.reserve(buffer_num);
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& buffer_state_list = render_pass_buffer_state_list[pass_index];
    auto render_pass_buffer_num = static_cast<uint32_t>(buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < render_pass_buffer_num; buffer_index++) {
      auto& buffer_state_config = buffer_state_list[buffer_index];
      auto& buffer_name = buffer_state_config.buffer_name;
      auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      if (initial_buffer_state_list.contains(buffer_id)) continue;
      const auto buffer_state = initial_buffer_state_list_with_buffer_name.contains(buffer_name) ? initial_buffer_state_list_with_buffer_name.at(buffer_name) : buffer_state_config.state;
      initial_buffer_state_list.insert_or_assign(buffer_id, buffer_state);
      if (initial_buffer_state_list.size() == buffer_num) break;
    }
    if (initial_buffer_state_list.size() == buffer_num) break;
  }
  return initial_buffer_state_list;
}
static unordered_map<BufferId, BufferStateFlags> ConfigureFinalBufferStateFlags(const unordered_map<StrId, BufferStateFlags>& final_buffer_state_list_with_buffer_name, const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, unordered_map<BufferId, BufferStateFlags>&& initial_buffer_state_list) {
  auto&& final_buffer_state_list = std::move(initial_buffer_state_list);
  const auto final_buffer_state_num = static_cast<uint32_t>(final_buffer_state_list_with_buffer_name.size());
  for (uint32_t pass_index = pass_num - 1; pass_index < pass_num /*pass_index is unsigned*/; pass_index--) {
    auto& buffer_state_list = render_pass_buffer_state_list[pass_index];
    auto render_pass_buffer_num = static_cast<uint32_t>(buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < render_pass_buffer_num; buffer_index++) {
      auto& buffer_state_config = buffer_state_list[buffer_index];
      auto& buffer_name = buffer_state_config.buffer_name;
      if (!final_buffer_state_list_with_buffer_name.contains(buffer_name)) continue;
      auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      if (final_buffer_state_list.contains(buffer_id)) continue;
      final_buffer_state_list.insert_or_assign(buffer_id, final_buffer_state_list_with_buffer_name.at(buffer_name));
      if (final_buffer_state_list.size() == final_buffer_state_num) break;
    }
    if (final_buffer_state_list.size() == final_buffer_state_num) break;
  }
  return std::move(final_buffer_state_list);
}
static unordered_map<BufferId, vector<BufferStateChangeInfo>> InitBufferStateChangeInfoList(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, const vector<BufferId>& buffer_id_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, const unordered_map<BufferId, BufferStateFlags>& initial_buffer_state_list, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<BufferId, vector<BufferStateChangeInfo>> buffer_state_change_info_list_map;
  buffer_state_change_info_list_map.reserve(buffer_id_list.size());
  for (auto& buffer_id : buffer_id_list) {
    buffer_state_change_info_list_map.insert_or_assign(buffer_id, vector<BufferStateChangeInfo>{memory_resource});
  }
  const uint32_t invalid_pass_index = 0xFFFFFFFF;
  unordered_map<BufferId, uint32_t> last_accessed_pass_index{memory_resource_work};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& buffer_state_list = render_pass_buffer_state_list[pass_index];
    const uint32_t buffer_num = static_cast<uint32_t>(buffer_state_list.size());
    auto& pass_binded_buffer_id_list = render_pass_buffer_id_list[pass_index];
    ASSERT(buffer_num == pass_binded_buffer_id_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      const auto buffer_id = pass_binded_buffer_id_list[buffer_index];
      auto& state_change_info_list = buffer_state_change_info_list_map.at(buffer_index);
      const auto barrier_exists = !state_change_info_list.empty();
      const auto current_state = barrier_exists ? state_change_info_list.back().state_after : initial_buffer_state_list.at(buffer_id);
      const auto next_state = buffer_state_list[buffer_index].state;
      if ((current_state & next_state) == next_state) {
        last_accessed_pass_index.insert_or_assign(buffer_id, pass_index);
        continue;
      }
      if (barrier_exists && IsBufferStateFlagsMergeable(current_state, next_state)) {
        state_change_info_list.back().state_after = MergeBufferStateFlags(current_state, next_state);
        last_accessed_pass_index.insert_or_assign(buffer_id, pass_index);
        continue;
      }
      if (barrier_exists && state_change_info_list.back().barrier_end_pass_index == invalid_pass_index) {
        auto& state_change_info = state_change_info_list.back();
        state_change_info.barrier_end_pass_index = pass_index;
        state_change_info.barrier_end_pass_pos_type = BarrierPosType::kPrePass;
      }
      const auto initial_access = !last_accessed_pass_index.contains(buffer_id);
      state_change_info_list.push_back({
          .barrier_begin_pass_index = initial_access ? 0 : last_accessed_pass_index.at(buffer_id),
          .barrier_end_pass_index = pass_index,
          .state_before = current_state,
          .state_after = next_state,
          .barrier_begin_pass_pos_type = initial_access ? BarrierPosType::kPrePass : BarrierPosType::kPostPass,
          .barrier_end_pass_pos_type = BarrierPosType::kPrePass,
        });
      last_accessed_pass_index.insert_or_assign(buffer_id, pass_index);
    }
  }
  return buffer_state_change_info_list_map;
}
static unordered_map<BufferId, vector<BufferStateChangeInfo>> AppendFinalBufferStateToChangeInfoList(const uint32_t pass_num, unordered_map<BufferId, vector<BufferStateChangeInfo>>&& buffer_state_change_info_list_map, const unordered_map<BufferId, BufferStateFlags>& final_buffer_state_list) {
  const auto end_pass_index = pass_num - 1;
  for (auto& [buffer_id, buffer_state_change_info_list] : buffer_state_change_info_list_map) {
    if (buffer_state_change_info_list.empty()) continue;
    auto& buffer_state_change_info = buffer_state_change_info_list.back();
    auto& buffer_state = final_buffer_state_list.at(buffer_id);
    if ((buffer_state_change_info.state_after & buffer_state) == buffer_state) continue;
    if (IsBufferStateFlagsMergeable(buffer_state_change_info.state_after, buffer_state)) {
      buffer_state_change_info.state_after = MergeBufferStateFlags(buffer_state_change_info.state_after, buffer_state);
      continue;
    }
    buffer_state_change_info_list.push_back({
        .barrier_begin_pass_index = buffer_state_change_info.barrier_end_pass_index,
        .barrier_end_pass_index = end_pass_index,
        .state_before = buffer_state_change_info.state_after,
        .state_after = buffer_state,
        .barrier_begin_pass_pos_type = BarrierPosType::kPostPass,
        .barrier_end_pass_pos_type = BarrierPosType::kPostPass,
      });
  }
  return std::move(buffer_state_change_info_list_map);
}
static unordered_map<BufferId, vector<BufferStateChangeInfo>> FixPassIndexAtSamePositionWithDifferentValues(unordered_map<BufferId, vector<BufferStateChangeInfo>>&& buffer_state_change_info_list_map) {
  for (auto&& [buffer_id, state_change_info_list] : buffer_state_change_info_list_map) {
    for (auto&& state_change_info : state_change_info_list) {
      if (state_change_info.barrier_begin_pass_index + 1 != state_change_info.barrier_end_pass_index) continue;
      if (state_change_info.barrier_begin_pass_pos_type != BarrierPosType::kPostPass) continue;
      if (state_change_info.barrier_end_pass_pos_type != BarrierPosType::kPrePass) continue;
      state_change_info.barrier_begin_pass_index = state_change_info.barrier_end_pass_index;
      state_change_info.barrier_begin_pass_pos_type = BarrierPosType::kPrePass;
    }
  }
  return std::move(buffer_state_change_info_list_map);
}
class RenderGraph {
 public:
  RenderGraph(RenderGraphConfig&& config, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work)
      : memory_resource_(memory_resource)
      , render_pass_num_(config.GetRenderPassNum())
  {
    auto& render_pass_buffer_state_list = config.GetRenderPassBufferStateList();
    std::tie(buffer_id_list_, render_pass_buffer_id_list_) = InitBufferIdList(render_pass_num_, render_pass_buffer_state_list, memory_resource_);
    // TODO decide creation state for buffers not in initial_buffer_state_list_with_buffer_name (in separate function)
    auto initial_buffer_state_list = ConfigureInitialBufferStateFlags(config.GetBufferInitialStateList(), render_pass_num_, render_pass_buffer_state_list, buffer_id_list_, render_pass_buffer_id_list_, memory_resource_work);
    buffer_state_change_info_list_map_ = InitBufferStateChangeInfoList(render_pass_num_, render_pass_buffer_state_list, buffer_id_list_, render_pass_buffer_id_list_, initial_buffer_state_list, memory_resource_, memory_resource_work);
    auto final_buffer_state_list = ConfigureFinalBufferStateFlags(config.GetBufferFinalStateList(), render_pass_num_, render_pass_buffer_state_list, render_pass_buffer_id_list_, std::move(initial_buffer_state_list));
    buffer_state_change_info_list_map_ = AppendFinalBufferStateToChangeInfoList(render_pass_num_, std::move(buffer_state_change_info_list_map_), final_buffer_state_list);
    buffer_state_change_info_list_map_ = FixPassIndexAtSamePositionWithDifferentValues(std::move(buffer_state_change_info_list_map_));
    // TODO find appropriate pass (in separate function)
  }
  constexpr uint32_t GetRenderPassNum() const { return render_pass_num_; }
  constexpr const auto& GetBufferIdList() const { return buffer_id_list_; }
  constexpr const auto& GetRenderPassBufferIdList() const { return render_pass_buffer_id_list_; }
  const auto& GetBufferStateChangeInfoList(const BufferId& buffer_id) const { return buffer_state_change_info_list_map_.at(buffer_id); }
 private:
  std::pmr::memory_resource* memory_resource_;
  uint32_t render_pass_num_;
  vector<BufferId> buffer_id_list_;
  vector<vector<BufferId>> render_pass_buffer_id_list_;
  unordered_map<BufferId, vector<BufferStateChangeInfo>> buffer_state_change_info_list_map_;
  RenderGraph() = delete;
  RenderGraph(const RenderGraph&) = delete;
  void operator=(const RenderGraph&) = delete;
};
using BarrierListPerPass = vector<vector<BarrierConfig>>;
static std::tuple<BarrierListPerPass, BarrierListPerPass> ConfigureBarriers(const RenderGraph& render_graph, std::pmr::memory_resource* memory_resource_barriers) {
  auto render_pass_num = render_graph.GetRenderPassNum();
  BarrierListPerPass barriers_pre_pass{memory_resource_barriers};
  barriers_pre_pass.resize(render_pass_num);
  BarrierListPerPass barriers_post_pass{memory_resource_barriers};
  barriers_post_pass.resize(render_pass_num);
  auto& buffer_id_list = render_graph.GetBufferIdList();
  for (auto& buffer_id : buffer_id_list) {
    auto& buffer_state_change_info_list = render_graph.GetBufferStateChangeInfoList(buffer_id);
    for (auto& buffer_state_change_info : buffer_state_change_info_list) {
      BarrierConfig barrier{
        .buffer_id = buffer_id,
        .split_type = BarrierSplitType::kNone,
        .params = BarrierTransition{
          .state_before = buffer_state_change_info.state_before,
          .state_after  = buffer_state_change_info.state_after,
        },
      };
      if (buffer_state_change_info.barrier_begin_pass_index == buffer_state_change_info.barrier_end_pass_index &&
          buffer_state_change_info.barrier_begin_pass_pos_type == buffer_state_change_info.barrier_end_pass_pos_type) {
        if (buffer_state_change_info.barrier_begin_pass_pos_type == BarrierPosType::kPrePass) {
          barriers_pre_pass[buffer_state_change_info.barrier_begin_pass_index].push_back(barrier);
        } else {
          barriers_post_pass[buffer_state_change_info.barrier_begin_pass_index].push_back(barrier);
        }
        continue;
      }
      barrier.split_type = BarrierSplitType::kBegin;
      if (buffer_state_change_info.barrier_begin_pass_pos_type == BarrierPosType::kPrePass) {
        barriers_pre_pass[buffer_state_change_info.barrier_begin_pass_index].push_back(barrier);
      } else {
        barriers_post_pass[buffer_state_change_info.barrier_begin_pass_index].push_back(barrier);
      }
      barrier.split_type = BarrierSplitType::kEnd;
      if (buffer_state_change_info.barrier_end_pass_pos_type == BarrierPosType::kPrePass) {
        barriers_pre_pass[buffer_state_change_info.barrier_end_pass_index].push_back(barrier);
      } else {
        barriers_post_pass[buffer_state_change_info.barrier_end_pass_index].push_back(barrier);
      }
    }
  }
  return {barriers_pre_pass, barriers_post_pass};
}
#if 0
struct BufferStateListPerBuffer {
  vector<uint32_t> pass_index_list;
  vector<BufferStateFlags> buffer_state_list;
};
static unordered_map<BufferId, BufferStateListPerBuffer> ConfigureRenderPassBufferUsages(const RenderPassBufferInfoList& render_pass_buffer_info_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, BufferStateListPerBuffer> buffer_usage_list{memory_resource};
  for (uint32_t pass_index = 0; auto&& render_pass_buffer_info_list_per_pass : render_pass_buffer_info_list) {
    for (auto&& buffer_info : render_pass_buffer_info_list_per_pass) {
      if (!buffer_usage_list.contains(buffer_info.buffer_id)) {
        buffer_usage_list.insert_or_assign(buffer_info.buffer_id, BufferStateListPerBuffer{vector<uint32_t>{memory_resource}, vector<BufferStateFlags>{memory_resource}});
      }
      buffer_usage_list.at(buffer_info.buffer_id).pass_index_list.push_back(pass_index);
      buffer_usage_list.at(buffer_info.buffer_id).buffer_state_list.push_back(buffer_info.state);
    }
    pass_index++;
  }
  return buffer_usage_list;
}
using BarrierUserPassIndexMap = unordered_map<BufferId, vector<uint32_t>>;
using BarrierListMap = unordered_map<BufferId, vector<BarrierConfig>>;
static std::tuple<BarrierUserPassIndexMap, BarrierListMap> ConfigureBarriersPerBuffer(const unordered_map<BufferId, BufferStateListPerBuffer>& buffer_state_list, std::pmr::memory_resource* memory_resource) {
  BarrierUserPassIndexMap barrier_user_pass_index_map{memory_resource};
  BarrierListMap barrier_list{memory_resource};
  for (auto& [buffer_id, state_info_list] : buffer_state_list) {
    const auto& state_num = state_info_list.pass_index_list.size();
    if (state_num <= 1) continue;
    ASSERT(state_num == state_info_list.buffer_state_list.size());
    vector<uint32_t> barrier_user_pass_index_per_buffer{memory_resource};
    vector<BarrierConfig> barrier_list_per_buffer{memory_resource};
    for (uint32_t i = 0; i < state_num - 1; i++) {
      auto barrier_exists = !barrier_list_per_buffer.empty();
      auto& current_state = barrier_exists ? std::get<BarrierTransition>(barrier_list_per_buffer.back().params).state_after : state_info_list.buffer_state_list[i];
      auto& next_state    = state_info_list.buffer_state_list[i + 1];
      if (current_state == next_state || (current_state & next_state) == next_state) continue;
      if (barrier_exists && IsBufferStateFlagsMergeable(current_state, next_state)) {
        std::get<BarrierTransition>(barrier_list_per_buffer.back().params).state_after = MergeBufferStateFlags(current_state, next_state);
        continue;
      }
      BarrierConfig barrier{
        .buffer_id = buffer_id,
        .split_type = BarrierSplitType::kNone,
        .params = BarrierTransition{
          .state_before = current_state,
          .state_after  = next_state,
        },
      };
      auto& current_pass_index = state_info_list.pass_index_list[i];
      auto& next_pass_index = state_info_list.pass_index_list[i + 1];
      if (current_pass_index + 1 >= next_pass_index)  {
        barrier_user_pass_index_per_buffer.push_back(current_pass_index);
        barrier_list_per_buffer.push_back(std::move(barrier));
      } else {
        barrier_user_pass_index_per_buffer.push_back(current_pass_index);
        barrier_user_pass_index_per_buffer.push_back(next_pass_index - 1);
        barrier.split_type = BarrierSplitType::kBegin;
        barrier_list_per_buffer.push_back(barrier);
        barrier.split_type = BarrierSplitType::kEnd;
        barrier_list_per_buffer.push_back(std::move(barrier));
      }
    }
    barrier_user_pass_index_map.insert_or_assign(buffer_id, std::move(barrier_user_pass_index_per_buffer));
    barrier_list.insert_or_assign(buffer_id, std::move(barrier_list_per_buffer));
  }
  return {barrier_user_pass_index_map, barrier_list};
}
using BarrierConfigList = vector<vector<BarrierConfig>>;
static BarrierConfigList ConfigureBarriersBetweenRenderPass(const BarrierUserPassIndexMap& barrier_user_pass_index_map, const BarrierListMap& barrier_list_map, const uint32_t render_pass_num, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  set<BufferId> buffer_id_list{memory_resource_work};
  for (auto& [buffer_id, list] : barrier_user_pass_index_map) {
    buffer_id_list.insert(buffer_id);
  }
  BarrierConfigList barriers{memory_resource};
  barriers.resize(render_pass_num + 1);
  for (auto& buffer_id : buffer_id_list) {
    auto& pass_index_list = barrier_user_pass_index_map.at(buffer_id);
    const auto& barrier_num = pass_index_list.size();
    ASSERT(barrier_list_map.contains(buffer_id));
    auto& barrier_list = barrier_list_map.at(buffer_id);
    ASSERT(barrier_num == barrier_list.size());
    for (uint32_t i = 0; i < barrier_num; i++) {
      barriers[pass_index_list[i]].push_back(barrier_list[i]);
    }
  }
  return barriers;
}
vector<vector<BarrierConfig>> ConfigureBarrier(const RenderPassBufferInfoList& pass_buffer_info_list, std::pmr::memory_resource* memory_resource_barrier, std::pmr::memory_resource* memory_resource_work) {
  auto buffer_usage_list = ConfigureRenderPassBufferUsages(pass_buffer_info_list, memory_resource_work);
  auto [barrier_user_pass_index_map, barrier_list_map] = ConfigureBarriersPerBuffer(buffer_usage_list, memory_resource_work);
  return ConfigureBarriersBetweenRenderPass(barrier_user_pass_index_map, barrier_list_map, 2, memory_resource_barrier, memory_resource_work);
}
SignalQueueRenderPassInfo ConfigureQueueSignal(const vector<CommandQueueType>& render_pass_command_queue_type, const RenderPassBufferReadWriteInfoList& pass_buffer_info_list, const RenderFrameLoopSetting loop_type, std::pmr::memory_resource* memory_resource_signal_info, std::pmr::memory_resource* memory_resource_work) {
  SignalQueueRenderPassInfo signal_queue_render_pass_info{memory_resource_signal_info};
  const auto& pass_len = render_pass_command_queue_type.size();
  unordered_map<BufferId, uint32_t>            prev_buffer_user_pass_index_list{memory_resource_work};
  unordered_map<BufferId, CommandQueueType>    prev_buffer_user_queue_list{memory_resource_work};
  unordered_map<BufferId, BufferReadWriteFlag> prev_buffer_read_write_flag_list{memory_resource_work};
  for (uint32_t pass_index = 0; pass_index < pass_len; pass_index++) {
    auto& command_queue_type = render_pass_command_queue_type[pass_index];
    auto& buffer_info_list = pass_buffer_info_list[pass_index];
    for (auto& buffer_info : buffer_info_list) {
      if (!prev_buffer_user_queue_list.contains(buffer_info.buffer_id)) continue;
      if (prev_buffer_user_queue_list.at(buffer_info.buffer_id) == command_queue_type) continue;
      if (buffer_info.read_write_flag == BufferReadWriteFlag::kRead && prev_buffer_read_write_flag_list.at(buffer_info.buffer_id) == BufferReadWriteFlag::kRead) continue;
      signal_queue_render_pass_info.insert_or_assign(prev_buffer_user_pass_index_list.at(buffer_info.buffer_id), pass_index);
    }
    for (auto& buffer_info : buffer_info_list) {
      prev_buffer_user_pass_index_list[buffer_info.buffer_id] = pass_index;
      prev_buffer_user_queue_list[buffer_info.buffer_id] = command_queue_type;
      prev_buffer_read_write_flag_list[buffer_info.buffer_id] = buffer_info.read_write_flag;
    }
  }
  if (loop_type == RenderFrameLoopSetting::kNoLoop) return signal_queue_render_pass_info;
  for (uint32_t pass_index = 0; pass_index < pass_len; pass_index++) {
    auto& command_queue_type = render_pass_command_queue_type[pass_index];
    auto& buffer_info_list = pass_buffer_info_list[pass_index];
    for (auto& buffer_info : buffer_info_list) {
      if (!prev_buffer_user_pass_index_list.contains(buffer_info.buffer_id)) continue;
      auto prev_pass_index = prev_buffer_user_pass_index_list.at(buffer_info.buffer_id);
      prev_buffer_user_pass_index_list.erase(buffer_info.buffer_id);
      if (pass_index >= prev_pass_index) continue;
      if (prev_buffer_user_queue_list.at(buffer_info.buffer_id) == command_queue_type) continue;
      if (buffer_info.read_write_flag == BufferReadWriteFlag::kRead && prev_buffer_read_write_flag_list.at(buffer_info.buffer_id) == BufferReadWriteFlag::kRead) continue;
      signal_queue_render_pass_info.insert_or_assign(prev_pass_index, pass_index);
    }
    if (prev_buffer_user_pass_index_list.empty()) break;
  }
  return signal_queue_render_pass_info;
}
SignalQueueRenderPassInfo RemoveRedundantQueueSignals(SignalQueueRenderPassInfo&& signal_queue_render_pass_info, [[maybe_unused]]const vector<CommandQueueType>& render_pass_command_queue_type, [[maybe_unused]]std::pmr::memory_resource* memory_resource_work) {
  // TODO remove [[maybe_unused]]
  // TODO implement
  return std::move(signal_queue_render_pass_info);
}
#endif
}
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_offset_in_bytes_persistent = 0;
const uint32_t buffer_size_in_bytes_persistent = 16 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_persistent + buffer_size_in_bytes_persistent;
const uint32_t buffer_size_in_bytes_scene = 4 * 1024;
const uint32_t buffer_offset_in_bytes_frame = buffer_offset_in_bytes_scene + buffer_size_in_bytes_scene;
const uint32_t buffer_size_in_bytes_frame = 4 * 1024;
const uint32_t buffer_offset_in_bytes_work = buffer_offset_in_bytes_frame + buffer_size_in_bytes_frame;
const uint32_t buffer_size_in_bytes_work = 16 * 1024;
std::byte buffer[buffer_offset_in_bytes_work + buffer_size_in_bytes_work]{};
}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include "doctest/doctest.h"
TEST_CASE("barrier for load from srv") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_size_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  CHECK(render_graph_config.GetRenderPassNum() == 0);
  auto render_pass_id = render_graph_config.CreateNewRenderPass();
  render_graph_config.AddNewBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag});
  CHECK(render_graph_config.GetRenderPassNum() == 1);
  auto prev_render_pass_id = render_pass_id;
  render_pass_id = render_graph_config.CreateNewRenderPass();
  CHECK(render_graph_config.GetRenderPassNum() == 2);
  CHECK(render_pass_id != prev_render_pass_id);
  render_graph_config.AddNewBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag});
  render_graph_config.AddNewBufferConfig(render_pass_id, {StrId("swapchain"),  kBufferStateFlagRtv, kWriteFlag});
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  {
    // check render graph config
    auto& render_pass_buffer_state_list = render_graph_config.GetRenderPassBufferStateList();
    auto render_pass_num = render_graph_config.GetRenderPassNum();
    auto [buffer_id_list, render_pass_buffer_id_list] = InitBufferIdList(render_pass_num, render_pass_buffer_state_list, &memory_resource_work);
    CHECK(buffer_id_list.size() == 2);
    CHECK(buffer_id_list[0] == 0);
    CHECK(buffer_id_list[1] == 1);
    CHECK(render_pass_buffer_id_list.size() == 2);
    CHECK(render_pass_buffer_id_list[0].size() == 1);
    CHECK(render_pass_buffer_id_list[0][0] == buffer_id_list[0]);
    CHECK(render_pass_buffer_id_list[1].size() == 2);
    CHECK(render_pass_buffer_id_list[1][0] == buffer_id_list[0]);
    CHECK(render_pass_buffer_id_list[1][1] == buffer_id_list[1]);
    auto initial_buffer_state_list = ConfigureInitialBufferStateFlags(render_graph_config.GetBufferInitialStateList(), render_pass_num, render_pass_buffer_state_list, buffer_id_list, render_pass_buffer_id_list, &memory_resource_work);
    CHECK(initial_buffer_state_list.size() == 2);
    CHECK(initial_buffer_state_list.contains(render_pass_buffer_id_list[0][0]));
    CHECK(initial_buffer_state_list.at(render_pass_buffer_id_list[0][0]) == kBufferStateFlagRtv);
    CHECK(initial_buffer_state_list.contains(render_pass_buffer_id_list[1][1]));
    CHECK(initial_buffer_state_list.at(render_pass_buffer_id_list[1][1]) == kBufferStateFlagPresent);
    const auto final_buffer_state_list = ConfigureFinalBufferStateFlags(render_graph_config.GetBufferFinalStateList(), render_pass_num, render_pass_buffer_state_list, render_pass_buffer_id_list, std::move(initial_buffer_state_list));
    CHECK(final_buffer_state_list.size() == 2);
    CHECK(final_buffer_state_list.contains(render_pass_buffer_id_list[0][0]));
    CHECK(final_buffer_state_list.at(render_pass_buffer_id_list[0][0]) == kBufferStateFlagRtv);
    CHECK(final_buffer_state_list.contains(render_pass_buffer_id_list[1][1]));
    CHECK(final_buffer_state_list.at(render_pass_buffer_id_list[1][1]) == kBufferStateFlagPresent);
    memory_resource_work.Reset();
  }
  RenderGraph render_graph(std::move(render_graph_config), &memory_resource_scene, &memory_resource_work);
  CHECK(render_graph.GetRenderPassNum() == 2);
  {
    auto& buffer_list = render_graph.GetBufferIdList();
    CHECK(buffer_list.size() == 2);
    CHECK(buffer_list[0] == 0);
    CHECK(buffer_list[1] == 1);
  }
  {
    auto& buffer_state_change_info_list = render_graph.GetBufferStateChangeInfoList(0);
    CHECK(buffer_state_change_info_list.size() == 2);
    {
      auto& buffer_state_change_info = buffer_state_change_info_list[0];
      CHECK(buffer_state_change_info.barrier_begin_pass_index == 1);
      CHECK(buffer_state_change_info.barrier_end_pass_index == 1);
      CHECK(buffer_state_change_info.state_before == kBufferStateFlagRtv);
      CHECK(buffer_state_change_info.state_after == kBufferStateFlagSrvPsOnly);
      CHECK(buffer_state_change_info.barrier_begin_pass_pos_type == BarrierPosType::kPrePass);
      CHECK(buffer_state_change_info.barrier_end_pass_pos_type == BarrierPosType::kPrePass);
    }
    {
      auto& buffer_state_change_info = buffer_state_change_info_list[1];
      CHECK(buffer_state_change_info.barrier_begin_pass_index == 1);
      CHECK(buffer_state_change_info.barrier_end_pass_index == 1);
      CHECK(buffer_state_change_info.state_before == kBufferStateFlagSrvPsOnly);
      CHECK(buffer_state_change_info.state_after == kBufferStateFlagRtv);
      CHECK(buffer_state_change_info.barrier_begin_pass_pos_type == BarrierPosType::kPostPass);
      CHECK(buffer_state_change_info.barrier_end_pass_pos_type == BarrierPosType::kPostPass);
    }
  }
  {
    auto& buffer_state_change_info_list = render_graph.GetBufferStateChangeInfoList(1);
    CHECK(buffer_state_change_info_list.size() == 2);
    {
      auto& buffer_state_change_info = buffer_state_change_info_list[0];
      CHECK(buffer_state_change_info.barrier_begin_pass_index == 0);
      CHECK(buffer_state_change_info.barrier_end_pass_index == 1);
      CHECK(buffer_state_change_info.state_before == kBufferStateFlagPresent);
      CHECK(buffer_state_change_info.state_after == kBufferStateFlagRtv);
      CHECK(buffer_state_change_info.barrier_begin_pass_pos_type == BarrierPosType::kPrePass);
      CHECK(buffer_state_change_info.barrier_end_pass_pos_type == BarrierPosType::kPrePass);
    }
    {
      auto& buffer_state_change_info = buffer_state_change_info_list[1];
      CHECK(buffer_state_change_info.barrier_begin_pass_index == 1);
      CHECK(buffer_state_change_info.barrier_end_pass_index == 1);
      CHECK(buffer_state_change_info.state_before == kBufferStateFlagRtv);
      CHECK(buffer_state_change_info.state_after == kBufferStateFlagPresent);
      CHECK(buffer_state_change_info.barrier_begin_pass_pos_type == BarrierPosType::kPostPass);
      CHECK(buffer_state_change_info.barrier_end_pass_pos_type == BarrierPosType::kPostPass);
    }
  }
  auto [barriers_prepass, barriers_postpass] = ConfigureBarriers(render_graph, &memory_resource_scene);
  CHECK(barriers_prepass.size() == 2);
  CHECK(barriers_prepass[0].size() == 1);
  CHECK(barriers_prepass[0][0].buffer_id == 1);
  CHECK(barriers_prepass[0][0].split_type == BarrierSplitType::kBegin);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[0][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_prepass[1].size() == 2);
  CHECK(barriers_prepass[1][0].buffer_id == 0);
  CHECK(barriers_prepass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_after  == kBufferStateFlagSrvPsOnly);
  CHECK(barriers_prepass[1][1].buffer_id == 1);
  CHECK(barriers_prepass[1][1].split_type == BarrierSplitType::kEnd);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass.size() == 2);
  CHECK(barriers_postpass[0].empty());
  CHECK(barriers_postpass[1].size() == 2);
  CHECK(barriers_postpass[1][0].buffer_id == 0);
  CHECK(barriers_postpass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][0].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_before == kBufferStateFlagSrvPsOnly);
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass[1][1].buffer_id == 1);
  CHECK(barriers_postpass[1][1].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_after  == kBufferStateFlagPresent);
}
#if 0
TEST_CASE("barrier for load from srv") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  RenderPassBufferInfo render_pass_buffer_info_initial{&memory_resource_work};
  render_pass_buffer_info_initial.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagPresent,
    });
  render_pass_buffer_info_initial.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagRtv,
    });
  RenderPassBufferInfo render_pass_buffer_info_draw{&memory_resource_work};
  render_pass_buffer_info_draw.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagRtv,
    });
  RenderPassBufferInfo render_pass_buffer_info_copy{&memory_resource_work};
  render_pass_buffer_info_copy.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagSrvPsOnly,
    });
  render_pass_buffer_info_copy.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagRtv,
    });
  RenderPassBufferInfo render_pass_buffer_info_final{&memory_resource_work};
  render_pass_buffer_info_final.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagRtv,
    });
  render_pass_buffer_info_final.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagPresent,
    });
  RenderPassBufferInfoList render_pass_buffer_info_list{
    {
      std::move(render_pass_buffer_info_draw),
      std::move(render_pass_buffer_info_copy),
    },
    &memory_resource_work
  };
  // TODO
  auto buffer_usage_list = ConfigureRenderPassBufferUsages(render_pass_buffer_info_list, std::move(render_pass_buffer_info_initial), std::move(render_pass_buffer_info_final), &memory_resource_work);
  CHECK(buffer_usage_list.size() == 2);
  CHECK(buffer_usage_list.contains(1));
  CHECK(buffer_usage_list.at(1).initial_state_flag == kBufferStateFlagRtv);
  CHECK(buffer_usage_list.at(1).final_state_flag   == kBufferStateFlagRtv);
  CHECK(buffer_usage_list.at(1).pass_index_list.size() == 2);
  CHECK(buffer_usage_list.at(1).pass_index_list[0] == 0);
  CHECK(buffer_usage_list.at(1).pass_index_list[1] == 1);
  CHECK(buffer_usage_list.at(1).buffer_state_list.size() == 2);
  CHECK(buffer_usage_list.at(1).buffer_state_list[0] == kBufferStateFlagRtv);
  CHECK(buffer_usage_list.at(1).buffer_state_list[1] == kBufferStateFlagSrvPsOnly);
  CHECK(buffer_usage_list.contains(2));
  CHECK(buffer_usage_list.at(2).initial_state_flag == kBufferStateFlagPresent);
  CHECK(buffer_usage_list.at(2).final_state_flag   == kBufferStateFlagPresent);
  CHECK(buffer_usage_list.at(2).pass_index_list.size() == 1);
  CHECK(buffer_usage_list.at(2).pass_index_list[0] == 1);
  CHECK(buffer_usage_list.at(2).buffer_state_list.size() == 1);
  CHECK(buffer_usage_list.at(2).buffer_state_list[0] == kBufferStateFlagRtv);
  auto [barrier_user_pass_index_map, buffer_barrier_list_map, barrier_pos_type_list_map] = ConfigureBarriersPerBuffer(buffer_usage_list, render_pass_command_queue_type, &memory_resource_work);
  CHECK(barrier_user_pass_index_map.size() == 2);
  CHECK(barrier_user_pass_index_map.contains(1));
  CHECK(barrier_user_pass_index_map.at(1).size() == 2);
  CHECK(barrier_user_pass_index_map.at(1)[0] == 1);
  CHECK(barrier_user_pass_index_map.at(1)[1] == 1);
  CHECK(barrier_user_pass_index_map.contains(2));
  CHECK(barrier_user_pass_index_map.at(2).size() == 3);
  CHECK(barrier_user_pass_index_map.at(2)[0] == 0);
  CHECK(barrier_user_pass_index_map.at(2)[1] == 1);
  CHECK(barrier_user_pass_index_map.at(2)[2] == 1);
  CHECK(barrier_pos_type_list_map.size() == 2);
  CHECK(barrier_pos_type_list_map.contains(1));
  CHECK(barrier_pos_type_list_map.at(1).size() == 2);
  CHECK(barrier_pos_type_list_map.at(1)[0] == BarrierPosType::kPrePass);
  CHECK(barrier_pos_type_list_map.at(1)[1] == BarrierPosType::kPostPass);
  CHECK(barrier_pos_type_list_map.contains(2));
  CHECK(barrier_pos_type_list_map.at(2).size() == 3);
  CHECK(barrier_pos_type_list_map.at(2)[0] == BarrierPosType::kPrePass);
  CHECK(barrier_pos_type_list_map.at(2)[1] == BarrierPosType::kPrePass);
  CHECK(barrier_pos_type_list_map.at(2)[2] == BarrierPosType::kPostPass);
  CHECK(buffer_barrier_list_map.size() == 2);
  CHECK(buffer_barrier_list_map.contains(1));
  CHECK(buffer_barrier_list_map.contains(2));
  CHECK(buffer_barrier_list_map.at(1).size() == 2);
  {
    auto& barrier = buffer_barrier_list_map.at(1)[0];
    CHECK(barrier.buffer_id == 1);
    CHECK(barrier.split_type == BarrierSplitType::kNone);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagRtv);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagSrvPsOnly);
  }
  {
    auto& barrier = buffer_barrier_list_map.at(1)[1];
    CHECK(barrier.buffer_id == 1);
    CHECK(barrier.split_type == BarrierSplitType::kNone);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagSrvPsOnly);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagRtv);
  }
  CHECK(buffer_barrier_list_map.at(2).size() == 3);
  {
    auto& barrier = buffer_barrier_list_map.at(2)[0];
    CHECK(barrier.buffer_id == 2);
    CHECK(barrier.split_type == BarrierSplitType::kBegin);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagPresent);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagRtv);
  }
  {
    auto& barrier = buffer_barrier_list_map.at(2)[1];
    CHECK(barrier.buffer_id == 2);
    CHECK(barrier.split_type == BarrierSplitType::kEnd);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagPresent);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagRtv);
  }
  {
    auto& barrier = buffer_barrier_list_map.at(2)[2];
    CHECK(barrier.buffer_id == 2);
    CHECK(barrier.split_type == BarrierSplitType::kNone);
    CHECK(std::get<BarrierTransition>(barrier.params).state_before == kBufferStateFlagRtv);
    CHECK(std::get<BarrierTransition>(barrier.params).state_after  == kBufferStateFlagPresent);
  }
  auto [barriers_prepass, barriers_postpass] = ConfigureBarriersBetweenRenderPass(barrier_user_pass_index_map, buffer_barrier_list_map, barrier_pos_type_list_map, 2, &memory_resource_work, &memory_resource_work);
  CHECK(barriers_prepass.size() == 2);
  CHECK(barriers_prepass[0].size() == 1);
  CHECK(barriers_prepass[0][0].buffer_id == 2);
  CHECK(barriers_prepass[0][0].split_type == BarrierSplitType::kBegin);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[0][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_prepass[1].size() == 2);
  CHECK(barriers_prepass[1][0].buffer_id == 1);
  CHECK(barriers_prepass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_after  == kBufferStateFlagSrvPsOnly);
  CHECK(barriers_prepass[1][1].buffer_id == 2);
  CHECK(barriers_prepass[1][1].split_type == BarrierSplitType::kEnd);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass.size() == 2);
  CHECK(barriers_postpass[0].empty());
  CHECK(barriers_postpass[1].size() == 2);
  CHECK(barriers_postpass[1][0].buffer_id == 1);
  CHECK(barriers_postpass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][0].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_before == kBufferStateFlagSrvPsOnly);
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass[1][1].buffer_id == 2);
  CHECK(barriers_postpass[1][1].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_after  == kBufferStateFlagPresent);
}
TEST_CASE("queue signal for use compute queue") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type{&memory_resource_work};
  render_pass_command_queue_type.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type.push_back(CommandQueueType::kGraphics);
  RenderPassBufferReadWriteInfoListPerPass render_pass_buffer_info_draw{&memory_resource_work};
  render_pass_buffer_info_draw.push_back({
      .buffer_id = 1,
      .read_write_flag = BufferReadWriteFlag::kWrite,
    });
  RenderPassBufferReadWriteInfoListPerPass render_pass_buffer_info_copy{&memory_resource_work};
  render_pass_buffer_info_copy.push_back({
      .buffer_id = 1,
      .read_write_flag = BufferReadWriteFlag::kRead,
    });
  RenderPassBufferReadWriteInfoList pass_buffer_info_list{
    {
      std::move(render_pass_buffer_info_draw),
      std::move(render_pass_buffer_info_copy),
    },
    &memory_resource_work
  };
  auto signal_queue_render_pass_info = ConfigureQueueSignal(render_pass_command_queue_type, pass_buffer_info_list, RenderFrameLoopSetting::kWithLoop, &memory_resource_work, &memory_resource_work);
  CHECK(signal_queue_render_pass_info.size() == 2);
  CHECK(signal_queue_render_pass_info.contains(0));
  CHECK(signal_queue_render_pass_info.at(0) == 1);
  CHECK(signal_queue_render_pass_info.contains(1));
  CHECK(signal_queue_render_pass_info.at(1) == 0);
}
TEST_CASE("barrier for use compute queue") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type{&memory_resource_work};
  render_pass_command_queue_type.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type.push_back(CommandQueueType::kGraphics);
  RenderPassBufferInfo render_pass_buffer_info_initial{&memory_resource_work};
  render_pass_buffer_info_initial.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagPresent,
    });
  RenderPassBufferInfo render_pass_buffer_info_draw{&memory_resource_work};
  render_pass_buffer_info_draw.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagUavWrite,
    });
  RenderPassBufferInfo render_pass_buffer_info_copy{&memory_resource_work};
  render_pass_buffer_info_copy.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagUavRead,
    });
  render_pass_buffer_info_copy.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagRtv,
    });
  RenderPassBufferInfo render_pass_buffer_info_final{&memory_resource_work};
  render_pass_buffer_info_final.push_back({
      .buffer_id = 1,
      .state = kBufferStateFlagRtv,
    });
  render_pass_buffer_info_final.push_back({
      .buffer_id = 2,
      .state = kBufferStateFlagPresent,
    });
  RenderPassBufferInfoList pass_buffer_info_list{
    {
      std::move(render_pass_buffer_info_initial),
      std::move(render_pass_buffer_info_draw),
      std::move(render_pass_buffer_info_copy),
      std::move(render_pass_buffer_info_final),
    },
    &memory_resource_work
  };
  // TODO
}
#endif
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
