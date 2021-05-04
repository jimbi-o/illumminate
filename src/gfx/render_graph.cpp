#include "render_graph.h"
namespace illuminate::gfx {
enum ReadWriteFlag : uint8_t {
  kReadFlag      = 0x1,
  kWriteFlag     = 0x2,
  kReadWriteFlag = (kReadFlag | kWriteFlag),
};
static BufferStateFlags MergeReadWriteFlag(const BufferStateFlags& state, const ReadWriteFlag& rw_flag) {
  BufferStateFlags ret = state;
  if (rw_flag & kReadFlag) ret = static_cast<BufferStateFlags>(ret | kBufferStateReadFlag);
  if (rw_flag & kWriteFlag) ret = static_cast<BufferStateFlags>(ret | kBufferStateWriteFlag);
  return ret;
}
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
  void AddNewBufferConfig(const uint32_t pass_index, RenderGraphBufferStateConfig&& config)  {
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
static std::tuple<vector<BufferId>, vector<vector<BufferId>>, vector<vector<BufferStateFlags>>> InitBufferIdList(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, std::pmr::memory_resource* memory_resource) {
  vector<BufferId> buffer_id_list{memory_resource};
  vector<vector<BufferId>> render_pass_buffer_id_list{memory_resource};
  render_pass_buffer_id_list.reserve(render_pass_buffer_state_list.size());
  vector<vector<BufferStateFlags>> render_pass_buffer_state_flag_list{memory_resource};
  render_pass_buffer_state_flag_list.reserve(render_pass_buffer_state_list.size());
  unordered_map<StrId, BufferId> used_buffer_name{memory_resource};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& buffer_state_list = render_pass_buffer_state_list[pass_index];
    render_pass_buffer_id_list.push_back(vector<BufferId>{memory_resource});
    render_pass_buffer_id_list.back().reserve(buffer_state_list.size());
    render_pass_buffer_state_flag_list.push_back(vector<BufferStateFlags>{memory_resource});
    render_pass_buffer_state_flag_list.back().reserve(buffer_state_list.size());
    for (auto& buffer_state : buffer_state_list) {
      if (!used_buffer_name.contains(buffer_state.buffer_name) || (buffer_state.read_write_flag & kReadFlag) == 0) {
        BufferId buffer_id = static_cast<BufferId>(used_buffer_name.size());
        used_buffer_name.insert_or_assign(buffer_state.buffer_name, buffer_id);
        buffer_id_list.push_back(buffer_id);
      }
      render_pass_buffer_id_list.back().push_back(used_buffer_name.at(buffer_state.buffer_name));
      if (buffer_state.state == kBufferStateFlagUav) {
        render_pass_buffer_state_flag_list.back().push_back(MergeReadWriteFlag(buffer_state.state, buffer_state.read_write_flag));
      } else {
        render_pass_buffer_state_flag_list.back().push_back(buffer_state.state);
      }
    }
  }
  return {buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list};
}
static std::tuple<unordered_map<BufferId, vector<BufferStateFlags>>, unordered_map<BufferId, vector<vector<uint32_t>>>> CreateBufferStateList(const uint32_t pass_num, const vector<BufferId>& buffer_id_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, const vector<vector<BufferStateFlags>>& render_pass_buffer_state_flag_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, vector<BufferStateFlags>> buffer_state_list{memory_resource};
  buffer_state_list.reserve(buffer_id_list.size());
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{memory_resource};
  buffer_user_pass_list.reserve(buffer_id_list.size());
  for (auto& buffer_id : buffer_id_list) {
    buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{memory_resource});
    buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{memory_resource});
    buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{memory_resource});
  }
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& current_pass_buffer_state_list = render_pass_buffer_state_flag_list[pass_index];
    const auto buffer_num = static_cast<uint32_t>(current_pass_buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      auto& buffer_state = current_pass_buffer_state_list[buffer_index];
      if (buffer_state_list.at(buffer_id).empty()) {
        buffer_state_list.at(buffer_id).push_back(buffer_state);
      }
      auto& prev_state = buffer_state_list.at(buffer_id).back();
      if ((buffer_state & prev_state) != prev_state) {
        if (IsBufferStateFlagsMergeable(buffer_state, prev_state)) {
          buffer_state_list.at(buffer_id).back() = MergeBufferStateFlags(buffer_state, prev_state);
        } else {
          buffer_state_list.at(buffer_id).push_back(buffer_state);
          buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{memory_resource});
        }
      }
      buffer_user_pass_list.at(buffer_id).back().push_back(pass_index);
    }
  }
  return {buffer_state_list, buffer_user_pass_list};
}
static unordered_map<BufferId, BufferStateFlags> ConvertBufferNameToBufferIdForInitialFlagList(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, const unordered_map<StrId, BufferStateFlags>& initial_state_flag_list_with_name, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<BufferId, BufferStateFlags> initial_state_flag_list{memory_resource};
  unordered_set<StrId> processed_buffer_names{memory_resource_work};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& current_pass_buffer_state_list = render_pass_buffer_state_list[pass_index];
    const auto buffer_num = static_cast<uint32_t>(current_pass_buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      auto& buffer_name = current_pass_buffer_state_list[buffer_index].buffer_name;
      if (!initial_state_flag_list_with_name.contains(buffer_name)) continue;
      if (processed_buffer_names.contains(buffer_name)) continue;
      auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      initial_state_flag_list.insert_or_assign(buffer_id, initial_state_flag_list_with_name.at(buffer_name));
      processed_buffer_names.insert(buffer_name);
      if (processed_buffer_names.size() == initial_state_flag_list_with_name.size()) break;
    }
    if (processed_buffer_names.size() == initial_state_flag_list_with_name.size()) break;
  }
  return initial_state_flag_list;
}
static std::tuple<unordered_map<BufferId, vector<BufferStateFlags>>, unordered_map<BufferId, vector<vector<uint32_t>>>> MergeInitialBufferState(const unordered_map<BufferId, BufferStateFlags>& initial_state_flag_list, unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  for (auto& [buffer_id, buffer_state] : initial_state_flag_list) {
    auto&& current_buffer_state_list = buffer_state_list.at(buffer_id);
    if (current_buffer_state_list.empty()) continue;
    auto&& current_buffer_state = current_buffer_state_list.front();
    if ((current_buffer_state & buffer_state) == buffer_state) continue;
    current_buffer_state_list.insert(current_buffer_state_list.begin(), buffer_state);
    auto&& current_buffer_user_pass_list = buffer_user_pass_list.at(buffer_id);
    current_buffer_user_pass_list.insert(current_buffer_user_pass_list.begin(), vector<uint32_t>{memory_resource});
  }
  return {buffer_state_list, buffer_user_pass_list};
}
static unordered_map<BufferId, BufferStateFlags> ConvertBufferNameToBufferIdForFinalFlagList(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, const unordered_map<StrId, BufferStateFlags>& final_state_flag_list_with_name, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<BufferId, BufferStateFlags> final_state_flag_list{memory_resource};
  unordered_set<StrId> processed_buffer_names{memory_resource_work};
  for (uint32_t pass_index = pass_num - 1; pass_index < pass_num /*pass_index is unsigned*/; pass_index--) {
    auto& current_pass_buffer_state_list = render_pass_buffer_state_list[pass_index];
    const auto buffer_num = static_cast<uint32_t>(current_pass_buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      auto& buffer_name = current_pass_buffer_state_list[buffer_index].buffer_name;
      if (!final_state_flag_list_with_name.contains(buffer_name)) continue;
      if (processed_buffer_names.contains(buffer_name)) continue;
      auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      final_state_flag_list.insert_or_assign(buffer_id, final_state_flag_list_with_name.at(buffer_name));
      processed_buffer_names.insert(buffer_name);
      if (processed_buffer_names.size() == final_state_flag_list_with_name.size()) break;
    }
    if (processed_buffer_names.size() == final_state_flag_list_with_name.size()) break;
  }
  return final_state_flag_list;
}
static std::tuple<unordered_map<BufferId, vector<BufferStateFlags>>, unordered_map<BufferId, vector<vector<uint32_t>>>> MergeFinalBufferState(const unordered_map<BufferId, BufferStateFlags>& final_state_flag_list, unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  for (auto& [buffer_id, buffer_state] : final_state_flag_list) {
    auto&& current_buffer_state_list = buffer_state_list.at(buffer_id);
    if (current_buffer_state_list.empty()) continue;
    auto&& current_buffer_state = current_buffer_state_list.back();
    if ((current_buffer_state & buffer_state) == buffer_state) continue;
    current_buffer_state_list.push_back(buffer_state);
    buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{memory_resource});
  }
  return {buffer_state_list, buffer_user_pass_list};
}
static std::tuple<unordered_map<BufferId, vector<BufferStateFlags>>, unordered_map<BufferId, vector<vector<uint32_t>>>> RevertBufferStateToInitialState(unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  for (auto& [buffer_id, user_list] : buffer_user_pass_list) {
    if (user_list.empty()) continue;
    if (user_list.back().empty()) continue;
    auto& state_list = buffer_state_list.at(buffer_id);
    BufferStateFlags initial_state = state_list.front();
    BufferStateFlags final_state = state_list.back();
    if ((initial_state & final_state) == initial_state) continue;
    state_list.push_back(initial_state);
    user_list.push_back(vector<uint32_t>{memory_resource});
  }
  return {buffer_state_list, buffer_user_pass_list};
}
static uint32_t FindClosestCommonDescendant(const vector<uint32_t>& pass_index_list) {
  return *std::max_element(pass_index_list.begin(), pass_index_list.end());
}
static uint32_t FindClosestCommonAncestor(const vector<uint32_t>& pass_index_list) {
  return *std::min_element(pass_index_list.begin(), pass_index_list.end());
}
enum CommandQueueTypeFlags : uint8_t {
  kCommandQueueTypeFlagsGraphics        = 0x01,
  kCommandQueueTypeFlagsCompute         = 0x02,
  kCommandQueueTypeFlagsTransfer        = 0x04,
  kCommandQueueTypeFlagsGraphicsCompute = kCommandQueueTypeFlagsGraphics | kCommandQueueTypeFlagsCompute,
  kCommandQueueTypeFlagsAll             = kCommandQueueTypeFlagsGraphicsCompute | kCommandQueueTypeFlagsTransfer,
};
static CommandQueueTypeFlags GetBufferStateValidCommandQueueTypeFlags(const BufferStateFlags state) {
  if (!(state & ~(kBufferStateFlagCopySrc | kBufferStateFlagCopyDst | kBufferStateFlagCommon))) return kCommandQueueTypeFlagsAll;
  if (state & kBufferStateFlagSrvPsOnly) return kCommandQueueTypeFlagsGraphics;
  if (state & kBufferStateFlagRtv) return kCommandQueueTypeFlagsGraphics;
  if (state & kBufferStateFlagDsvWrite) return kCommandQueueTypeFlagsGraphics;
  if (state & kBufferStateFlagDsvRead) return kCommandQueueTypeFlagsGraphics;
  return kCommandQueueTypeFlagsGraphicsCompute;
}
static uint32_t FindClosestCommonDescendant(const vector<uint32_t>& ancestors, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, const unordered_map<uint32_t, CommandQueueTypeFlags>& render_pass_command_queue_type_list, const CommandQueueTypeFlags valid_queues, std::pmr::memory_resource* memory_resource_work) {
  if (ancestors.empty()) {
    // return pass without ancestor
    uint32_t retval = ~0u;
    for (auto& [pass_index, distance_map] :node_distance_map) {
      if (pass_index > retval) continue;
      if (!(render_pass_command_queue_type_list.at(pass_index) & valid_queues)) continue;
      bool valid = true;
      for (auto& [target, distance] : distance_map) {
        if (distance < 0) {
          valid = false;
          break;
        }
      }
      if (valid) {
        retval = pass_index;
      }
    }
    return retval;
  }
  if (ancestors.size() == 1 && (render_pass_command_queue_type_list.at(ancestors.back()) & valid_queues)) return ancestors.back();
  unordered_set<uint32_t> merged_ancestors{memory_resource_work};
  merged_ancestors.reserve(ancestors.size());
  merged_ancestors.insert(ancestors.begin(), ancestors.end());
  // erase ancestors within ancestors
  for (auto& a : ancestors) {
    auto& distance_map = node_distance_map.at(a);
    for (auto& b : ancestors) {
      if (merged_ancestors.contains(a) && distance_map.contains(b) && distance_map.at(b) > 0) {
        merged_ancestors.erase(a);
        break;
      }
    }
  }
  if (merged_ancestors.size() == 1 && (render_pass_command_queue_type_list.at(*merged_ancestors.begin()) & valid_queues)) return *merged_ancestors.begin();
  // gather all descendants of merged_ancestors
  unordered_set<uint32_t> cands{memory_resource_work};
  for (auto& ancestor : merged_ancestors) {
    auto& distance_map = node_distance_map.at(ancestor);
    for (auto& [pass_index, distance] : distance_map) {
      if (distance < 0) continue;
      if (render_pass_command_queue_type_list.at(pass_index) & valid_queues) cands.insert(pass_index);
    }
  }
  // remove descendants not in all merged_ancestors
  auto cands_it = cands.begin();
  while (cands_it != cands.end()) {
    bool succeed_to_next = true;
    for (auto& ancestor : merged_ancestors) {
      auto& distance_map = node_distance_map.at(ancestor);
      if (!distance_map.contains(*cands_it) || distance_map.at(*cands_it) < 0) {
        cands_it = cands.erase(cands_it);
        succeed_to_next = false;
        break;
      }
    }
    if (succeed_to_next) cands_it++;
  }
  if (cands.size() == 1) return *cands.begin();
  // select descendant closest to any merged_ancestors
  auto retval = *cands.begin();
  auto min_dist = std::numeric_limits<int>::min();
  for (auto& cand : cands) {
    auto& dist_from_cand = node_distance_map.at(cand);
    for (auto& ancestor : merged_ancestors) {
      if (dist_from_cand.at(ancestor) > min_dist) {
        min_dist = dist_from_cand.at(ancestor);
        retval = cand;
      }
    }
  }
  return retval;
}
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
static unordered_map<BufferId, vector<BufferStateChangeInfo>> CreateBufferStateChangeInfoList(const uint32_t pass_num, const vector<CommandQueueType>& render_pass_command_queue_type_list, const unordered_map<BufferId, vector<BufferStateFlags>>& buffer_state_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, vector<BufferStateChangeInfo>> buffer_state_change_info_list_map{memory_resource};
  for (auto& [buffer_id, current_buffer_state_list] : buffer_state_list) {
    buffer_state_change_info_list_map.insert_or_assign(buffer_id, vector<BufferStateChangeInfo>{memory_resource});
    auto& current_buffer_state_change_info_list = buffer_state_change_info_list_map.at(buffer_id);
    auto& current_buffer_user_pass_list = buffer_user_pass_list.at(buffer_id);
    const auto buffer_state_num = static_cast<uint32_t>(current_buffer_user_pass_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_state_num - 1; buffer_index++) {
      auto& current_state = current_buffer_state_list[buffer_index];
      auto& next_state = current_buffer_state_list[buffer_index + 1];
      auto valid_command_queue_type_flag = (GetBufferStateValidCommandQueueTypeFlags(current_state) & GetBufferStateValidCommandQueueTypeFlags(next_state));
      auto& current_users = current_buffer_user_pass_list[buffer_index];
      auto& next_users = current_buffer_user_pass_list[buffer_index + 1];
      auto begin_pass = (buffer_index == 0 && current_users.empty()) ? 0 :FindClosestCommonDescendant(current_users);
      auto end_pass = (buffer_index + 2 == buffer_state_num && next_users.empty()) ? (pass_num - 1) : FindClosestCommonAncestor(next_users);
      current_buffer_state_change_info_list.push_back({
          .barrier_begin_pass_index = begin_pass,
          .barrier_end_pass_index = end_pass,
          .state_before = current_state,
          .state_after = next_state,
          .barrier_begin_pass_pos_type = core::IsContaining(current_users, begin_pass) ? BarrierPosType::kPostPass : BarrierPosType::kPrePass,
          .barrier_end_pass_pos_type = core::IsContaining(next_users, end_pass) ? BarrierPosType::kPrePass : BarrierPosType::kPostPass,
        });
      auto&& current_buffer_state_change_info = current_buffer_state_change_info_list.back();
      if (current_buffer_state_change_info.barrier_begin_pass_index + 1 != current_buffer_state_change_info.barrier_end_pass_index) continue;
      if (current_buffer_state_change_info.barrier_begin_pass_pos_type != BarrierPosType::kPostPass) continue;
      if (current_buffer_state_change_info.barrier_end_pass_pos_type != BarrierPosType::kPrePass) continue;
      current_buffer_state_change_info.barrier_begin_pass_index++;
      current_buffer_state_change_info.barrier_begin_pass_pos_type = current_buffer_state_change_info.barrier_end_pass_pos_type;
    }
  }
  return buffer_state_change_info_list_map;
}
class RenderGraph {
 public:
  RenderGraph(RenderGraphConfig&& config, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work)
      : memory_resource_(memory_resource)
      , render_pass_num_(config.GetRenderPassNum())
  {
    vector<vector<BufferId>> render_pass_buffer_id_list{memory_resource_work};
    vector<vector<BufferStateFlags>> render_pass_buffer_state_flag_list{memory_resource_work};
    std::tie(buffer_id_list_, render_pass_buffer_id_list, render_pass_buffer_state_flag_list) = InitBufferIdList(render_pass_num_, config.GetRenderPassBufferStateList(), memory_resource_);
    auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num_, buffer_id_list_, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, memory_resource_work);
    auto initial_state_flag_list = ConvertBufferNameToBufferIdForInitialFlagList(render_pass_num_, config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, config.GetBufferInitialStateList(), memory_resource_work, memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
    auto final_state_flag_list = ConvertBufferNameToBufferIdForFinalFlagList(render_pass_num_, config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, config.GetBufferFinalStateList(), memory_resource_work, memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = RevertBufferStateToInitialState(std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
#if 0
    // TODO
    buffer_state_change_info_list_map_ = CreateBufferStateChangeInfoList(render_pass_num_, buffer_state_list, buffer_user_pass_list, memory_resource);
#endif
  }
  constexpr uint32_t GetRenderPassNum() const { return render_pass_num_; }
  constexpr const auto& GetBufferIdList() const { return buffer_id_list_; }
  const auto& GetBufferStateChangeInfoList(const BufferId& buffer_id) const { return buffer_state_change_info_list_map_.at(buffer_id); }
  // TODO create function to return initial creation state for buffers not in initial_buffer_state_list_with_buffer_name (in separate function)
 private:
  std::pmr::memory_resource* memory_resource_;
  uint32_t render_pass_num_;
  vector<BufferId> buffer_id_list_;
  unordered_map<BufferId, vector<uint32_t>> buffer_user_pass_index_;
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
TEST_CASE("graph node test") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<uint32_t, unordered_map<uint32_t, int32_t>> node_distance_map;
  node_distance_map.insert_or_assign(0, unordered_map<uint32_t, int32_t>{});
  node_distance_map.at(0).insert_or_assign(0, 0);
  node_distance_map.at(0).insert_or_assign(1, 1);
  node_distance_map.at(0).insert_or_assign(2, 2);
  node_distance_map.insert_or_assign(1, unordered_map<uint32_t, int32_t>{});
  node_distance_map.at(1).insert_or_assign(0, -1);
  node_distance_map.at(1).insert_or_assign(1, 0);
  node_distance_map.at(1).insert_or_assign(2, 1);
  node_distance_map.insert_or_assign(2, unordered_map<uint32_t, int32_t>{});
  node_distance_map.at(2).insert_or_assign(0, -2);
  node_distance_map.at(2).insert_or_assign(1, -1);
  node_distance_map.at(2).insert_or_assign(2, 0);
  vector<uint32_t> ancestors;
  ancestors.push_back(0);
  unordered_map<uint32_t, CommandQueueTypeFlags> render_pass_command_queue_type_list;
  render_pass_command_queue_type_list.insert_or_assign(0, kCommandQueueTypeFlagsGraphics);
  render_pass_command_queue_type_list.insert_or_assign(1, kCommandQueueTypeFlagsGraphics);
  render_pass_command_queue_type_list.insert_or_assign(2, kCommandQueueTypeFlagsGraphics);
  CommandQueueTypeFlags valid_queues{kCommandQueueTypeFlagsAll};
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 0);
  ancestors.push_back(1);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 1);
  ancestors.push_back(2);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 2);
  ancestors.clear();
  ancestors.push_back(0);
  render_pass_command_queue_type_list.insert_or_assign(0, kCommandQueueTypeFlagsCompute);
  valid_queues = kCommandQueueTypeFlagsGraphics;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 1);
  node_distance_map.insert_or_assign(3, unordered_map<uint32_t, int32_t>{});
  node_distance_map.at(3).insert_or_assign(3, 0);
  node_distance_map.at(3).insert_or_assign(1, 1);
  node_distance_map.at(3).insert_or_assign(2, 2);
  node_distance_map.at(1).insert_or_assign(3, -1);
  node_distance_map.at(2).insert_or_assign(3, -2);
  render_pass_command_queue_type_list.insert_or_assign(3, kCommandQueueTypeFlagsGraphics);
  ancestors.clear();
  ancestors.push_back(0);
  ancestors.push_back(3);
  valid_queues = kCommandQueueTypeFlagsAll;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 1);
  valid_queues = kCommandQueueTypeFlagsGraphics;
  render_pass_command_queue_type_list.insert_or_assign(1, kCommandQueueTypeFlagsCompute);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 2);
  ancestors.clear();
  ancestors.push_back(0);
  ancestors.push_back(1);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 2);
  ancestors.clear();
  valid_queues = kCommandQueueTypeFlagsAll;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 0);
  ancestors.clear();
  valid_queues = kCommandQueueTypeFlagsGraphics;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues, &memory_resource_work) == 3);
}
TEST_CASE("barrier for load from srv") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_size_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  CHECK(render_graph_config.GetRenderPassNum() == 0);
  auto render_pass_id = render_graph_config.CreateNewRenderPass({.pass_name = StrId("draw"), .command_queue_type = CommandQueueType::kGraphics});
  render_graph_config.AddNewBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag});
  CHECK(render_graph_config.GetRenderPassNum() == 1);
  auto prev_render_pass_id = render_pass_id;
  render_pass_id = render_graph_config.CreateNewRenderPass({.pass_name = StrId("copy"), .command_queue_type = CommandQueueType::kGraphics});
  CHECK(render_graph_config.GetRenderPassNum() == 2);
  CHECK(render_pass_id != prev_render_pass_id);
  render_graph_config.AddNewBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag});
  render_graph_config.AddNewBufferConfig(render_pass_id, {StrId("swapchain"),  kBufferStateFlagRtv, kWriteFlag});
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  {
    // check render graph config
    auto render_pass_num = render_graph_config.GetRenderPassNum();
    auto [buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list] = InitBufferIdList(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), &memory_resource_work);
    CHECK(buffer_id_list.size() == 2);
    CHECK(buffer_id_list[0] == 0);
    CHECK(buffer_id_list[1] == 1);
    CHECK(render_pass_buffer_id_list.size() == 2);
    CHECK(render_pass_buffer_id_list[0].size() == 1);
    CHECK(render_pass_buffer_id_list[0][0] == buffer_id_list[0]);
    CHECK(render_pass_buffer_id_list[1].size() == 2);
    CHECK(render_pass_buffer_id_list[1][0] == buffer_id_list[0]);
    CHECK(render_pass_buffer_id_list[1][1] == buffer_id_list[1]);
    CHECK(render_pass_buffer_state_flag_list.size() == 2);
    CHECK(render_pass_buffer_state_flag_list[0].size() == 1);
    CHECK(render_pass_buffer_state_flag_list[0][0] == kBufferStateFlagRtv);
    CHECK(render_pass_buffer_state_flag_list[1].size() == 2);
    CHECK(render_pass_buffer_state_flag_list[1][0] == kBufferStateFlagSrvPsOnly);
    CHECK(render_pass_buffer_state_flag_list[1][1] == kBufferStateFlagRtv);
    auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num, buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, &memory_resource_work);
    auto initial_state_flag_list = ConvertBufferNameToBufferIdForInitialFlagList(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, render_graph_config.GetBufferInitialStateList(), &memory_resource_work, &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    auto final_state_flag_list = ConvertBufferNameToBufferIdForFinalFlagList(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, render_graph_config.GetBufferFinalStateList(), &memory_resource_work, &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    CHECK(buffer_state_list.size() == 2);
    CHECK(buffer_user_pass_list.size() == 2);
    CHECK(buffer_state_list.contains(0));
    CHECK(buffer_state_list.at(0).size() == 2);
    CHECK(buffer_state_list.at(0)[0] == kBufferStateFlagRtv);
    CHECK(buffer_state_list.at(0)[1] == kBufferStateFlagSrvPsOnly);
    CHECK(buffer_user_pass_list.contains(0));
    CHECK(buffer_user_pass_list.at(0).size() == 2);
    CHECK(buffer_user_pass_list.at(0)[0].size() == 1);
    CHECK(buffer_user_pass_list.at(0)[0][0] == 0);
    CHECK(buffer_user_pass_list.at(0)[1].size() == 1);
    CHECK(buffer_user_pass_list.at(0)[1][0] == 1);
    CHECK(buffer_state_list.contains(1));
    CHECK(buffer_state_list.at(1).size() == 3);
    CHECK(buffer_state_list.at(1)[0] == kBufferStateFlagPresent);
    CHECK(buffer_state_list.at(1)[1] == kBufferStateFlagRtv);
    CHECK(buffer_state_list.at(1)[2] == kBufferStateFlagPresent);
    CHECK(buffer_user_pass_list.contains(1));
    CHECK(buffer_user_pass_list.at(1).size() == 3);
    CHECK(buffer_user_pass_list.at(1)[0].empty());
    CHECK(buffer_user_pass_list.at(1)[1].size() == 1);
    CHECK(buffer_user_pass_list.at(1)[1][0] == 1);
    CHECK(buffer_user_pass_list.at(1)[2].empty());
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
TEST_CASE("barrier for use compute queue") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_size_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("draw"), .command_queue_type = CommandQueueType::kCompute});
  CHECK(render_graph_config.GetRenderPassIndex(StrId("draw")) == 0);
  render_graph_config.AddNewBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagUav, kWriteFlag});
  render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("present"), .command_queue_type = CommandQueueType::kGraphics});
  render_graph_config.AddNewBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagUav, kReadFlag});
  render_graph_config.AddNewBufferConfig(render_pass_index, {StrId("swapchain"), kBufferStateFlagRtv, kWriteFlag});
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  CHECK(render_graph_config.GetRenderPassIndex(StrId("draw")) == 0);
  CHECK(render_graph_config.GetRenderPassIndex(StrId("present")) == 1);
  CHECK(render_graph_config.GetRenderPassIndex(StrId("present")) == render_pass_index);
  RenderGraph render_graph(std::move(render_graph_config), &memory_resource_scene, &memory_resource_work);
  auto [barriers_prepass, barriers_postpass] = ConfigureBarriers(render_graph, &memory_resource_scene);
  CHECK(barriers_prepass.size() == 2);
  CHECK(barriers_prepass[0].empty());
  CHECK(barriers_prepass[1].size() == 2);
  CHECK(barriers_prepass[1][0].buffer_id == 0);
  CHECK(barriers_prepass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierUav>(barriers_prepass[1][0].params));
  CHECK(barriers_prepass[1][1].buffer_id == 1);
  CHECK(barriers_prepass[1][1].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass.size() == 2);
  CHECK(barriers_postpass[0].empty());
  CHECK(barriers_postpass[1].size() == 1);
  CHECK(barriers_postpass[1][0].buffer_id == 1);
  CHECK(barriers_postpass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][0].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_after  == kBufferStateFlagPresent);
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
