#include "render_graph.h"
namespace illuminate::gfx {
constexpr auto IsBufferStateFlagMergeAcceptable(const BufferStateFlags& state) {
  if ((state & kBufferStateFlagUav) != 0) { return false; }
  if ((state & kBufferStateFlagRtv) != 0) { return false; }
  if ((state & kBufferStateFlagDsvWrite) != 0) { return false; }
  if ((state & kBufferStateFlagCopyDst) != 0) { return false; }
  if ((state & kBufferStateFlagPresent) != 0) { return false; }
  return true;
}
constexpr auto IsBufferStateFlagsMergeable(const BufferStateFlags& a, const BufferStateFlags& b) {
  if (!IsBufferStateFlagMergeAcceptable(a)) { return false; }
  if (!IsBufferStateFlagMergeAcceptable(b)) { return false; }
  return true;
}
constexpr auto MergeBufferStateFlags(const BufferStateFlags& a, const BufferStateFlags& b) {
  return static_cast<BufferStateFlags>(a | b);
}
static auto MergeReadWriteFlag(const BufferStateFlags& state, const ReadWriteFlag& rw_flag) {
  BufferStateFlags ret = state;
  if ((rw_flag & kReadFlag) != 0) { ret = static_cast<BufferStateFlags>(ret | kBufferStateReadFlag); }
  if ((rw_flag & kWriteFlag) != 0) { ret = static_cast<BufferStateFlags>(ret | kBufferStateWriteFlag); }
  return ret;
}
constexpr auto IsReadOnlyBuffer(const BufferStateFlags& state, const ReadWriteFlag& read_write_flag) {
  if ((read_write_flag & kWriteFlag) != 0) { return false; }
  if ((state & kBufferStateFlagCbvUpload) == 0) { return true; }
  if ((state & kBufferStateFlagSrvPsOnly) == 0) { return true; }
  if ((state & kBufferStateFlagSrvNonPs) == 0) { return true; }
  if ((state & kBufferStateFlagCopySrc) == 0) { return true; }
  if ((state & kBufferStateFlagPresent) == 0) { return true; }
  return false;
}
constexpr auto IsBufferInWritableState(const BufferStateFlags& state) {
  if ((state & kBufferStateWriteFlag) != 0) { return true; }
  if ((state & kBufferStateFlagRtv) != 0) { return true; }
  if ((state & kBufferStateFlagCopyDst) != 0) { return true; }
  return false;
}
constexpr auto IsBufferInReadableState(const BufferStateFlags& state) {
  if ((state & kBufferStateReadFlag) != 0) { return true; }
  if ((state & kBufferStateFlagCbvUpload) != 0) { return true; }
  if ((state & kBufferStateFlagSrvPsOnly) != 0) { return true; }
  if ((state & kBufferStateFlagSrvNonPs) != 0) { return true; }
  if ((state & kBufferStateFlagCopySrc) != 0) { return true; }
  if ((state & kBufferStateFlagPresent) != 0) { return true; }
  return false;
}
static auto InitBufferIdList(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  vector<BufferId> buffer_id_list{memory_resource};
  vector<vector<BufferId>> render_pass_buffer_id_list{memory_resource};
  render_pass_buffer_id_list.reserve(render_pass_buffer_state_list.size());
  vector<vector<BufferStateFlags>> render_pass_buffer_state_flag_list{memory_resource};
  render_pass_buffer_state_flag_list.reserve(render_pass_buffer_state_list.size());
  vector<unordered_map<BufferId, BufferClearType>> render_pass_buffer_clear_info{memory_resource};
  render_pass_buffer_clear_info.reserve(render_pass_buffer_state_list.size());
  unordered_map<StrId, BufferId> used_buffer_name{memory_resource_work};
  BufferId next_buffer_id = 0;
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    const auto& buffer_state_list = render_pass_buffer_state_list[pass_index];
    render_pass_buffer_id_list.push_back(vector<BufferId>{memory_resource});
    render_pass_buffer_id_list.back().reserve(buffer_state_list.size());
    render_pass_buffer_state_flag_list.push_back(vector<BufferStateFlags>{memory_resource});
    render_pass_buffer_state_flag_list.back().reserve(buffer_state_list.size());
    render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{memory_resource});
    for (const auto& buffer_state : buffer_state_list) {
      if (!used_buffer_name.contains(buffer_state.buffer_name) || (buffer_state.read_write_flag & kReadFlag) == 0) {
        if (IsReadOnlyBuffer(buffer_state.state, buffer_state.read_write_flag)) {
          logwarn("InitBufferIdList IsReadOnlyBuffer {} {} {} {} {}", pass_index, buffer_state.buffer_name, next_buffer_id, buffer_state.state, buffer_state.read_write_flag, buffer_state.buffer_clear_flag);
        }
        used_buffer_name.insert_or_assign(buffer_state.buffer_name, next_buffer_id);
        buffer_id_list.push_back(next_buffer_id);
        next_buffer_id++;
      }
      render_pass_buffer_id_list.back().push_back(used_buffer_name.at(buffer_state.buffer_name));
      if ((buffer_state.state & kBufferStateFlagUav) != 0 || (buffer_state.state & kBufferStateFlagDsv) != 0) {
        render_pass_buffer_state_flag_list.back().push_back(MergeReadWriteFlag(buffer_state.state, buffer_state.read_write_flag));
      } else {
        render_pass_buffer_state_flag_list.back().push_back(buffer_state.state);
      }
      if (buffer_state.buffer_clear_flag == BufferClearFlag::kClear) {
        if (IsReadOnlyBuffer(buffer_state.state, buffer_state.read_write_flag)) {
          logwarn("InitBufferIdList read only buffer with clear flag on {} {} {} {} {}", pass_index, buffer_state.buffer_name, next_buffer_id, buffer_state.state, buffer_state.read_write_flag, buffer_state.buffer_clear_flag);
        } else {
          render_pass_buffer_clear_info.back().insert_or_assign(render_pass_buffer_id_list.back().back(), BufferClearType::kClear);
        }
      }
    }
  }
  return std::make_tuple(buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, render_pass_buffer_clear_info);
}
static auto CreateBufferStateList(const uint32_t pass_num, const vector<BufferId>& buffer_id_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, const vector<vector<BufferStateFlags>>& render_pass_buffer_state_flag_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, vector<BufferStateFlags>> buffer_state_list{memory_resource};
  buffer_state_list.reserve(buffer_id_list.size());
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{memory_resource};
  buffer_user_pass_list.reserve(buffer_id_list.size());
  for (const auto& buffer_id : buffer_id_list) {
    buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{memory_resource});
    buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{memory_resource});
    buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{memory_resource});
  }
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    const auto& current_pass_buffer_state_list = render_pass_buffer_state_flag_list[pass_index];
    const auto buffer_num = static_cast<uint32_t>(current_pass_buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      const auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      const auto& buffer_state = current_pass_buffer_state_list[buffer_index];
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
  return std::make_tuple(buffer_state_list, buffer_user_pass_list);
}
static auto GetBufferNameIdMap(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<StrId, unordered_set<BufferId>> buffer_name_id_map{memory_resource};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    const auto& current_pass_buffer_state_list = render_pass_buffer_state_list[pass_index];
    const auto buffer_num = static_cast<uint32_t>(current_pass_buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      const auto& name = current_pass_buffer_state_list[buffer_index].buffer_name;
      const auto& id = render_pass_buffer_id_list[pass_index][buffer_index];
      if (!buffer_name_id_map.contains(name)) {
        buffer_name_id_map.insert_or_assign(name, unordered_set<BufferId>{memory_resource});
      }
      buffer_name_id_map.at(name).insert(id);
    }
  }
  return buffer_name_id_map;
}
static auto ConvertBufferNameToBufferIdForBufferStateFlagList(const unordered_map<StrId, unordered_set<BufferId>>& buffer_name_id_map, const unordered_map<StrId, BufferStateFlags>& state_flag_list_with_name, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, BufferStateFlags> state_flag_list{memory_resource};
  for (const auto& [buffer_name, flag] : state_flag_list_with_name) {
    for (const auto& buffer_id : buffer_name_id_map.at(buffer_name)) {
      state_flag_list.insert_or_assign(buffer_id, flag);
    }
  }
  return state_flag_list;
}
static auto MergeInitialBufferState(const unordered_map<BufferId, BufferStateFlags>& initial_state_flag_list, unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  for (const auto& [buffer_id, buffer_state] : initial_state_flag_list) {
    auto&& current_buffer_state_list = buffer_state_list.at(buffer_id);
    if (current_buffer_state_list.empty()) { continue; }
    auto&& current_buffer_state = current_buffer_state_list.front();
    if ((current_buffer_state & buffer_state) == buffer_state) { continue; }
    current_buffer_state_list.insert(current_buffer_state_list.begin(), buffer_state);
    auto&& current_buffer_user_pass_list = buffer_user_pass_list.at(buffer_id);
    current_buffer_user_pass_list.insert(current_buffer_user_pass_list.begin(), vector<uint32_t>{memory_resource});
  }
  return std::make_tuple(buffer_state_list, buffer_user_pass_list);
}
static auto MergeFinalBufferState(const unordered_map<BufferId, BufferStateFlags>& final_state_flag_list, unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  for (const auto& [buffer_id, buffer_state] : final_state_flag_list) {
    auto&& current_buffer_state_list = buffer_state_list.at(buffer_id);
    if (current_buffer_state_list.empty()) { continue; }
    auto&& current_buffer_state = current_buffer_state_list.back();
    if ((current_buffer_state & buffer_state) == buffer_state) { continue; }
    current_buffer_state_list.push_back(buffer_state);
    buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{memory_resource});
  }
  return std::make_tuple(buffer_state_list, buffer_user_pass_list);
}
static auto RevertBufferStateToInitialState(unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  for (auto& [buffer_id, user_list] : buffer_user_pass_list) {
    if (user_list.empty()) { continue; }
    if (user_list.back().empty()) { continue; }
    auto& state_list = buffer_state_list.at(buffer_id);
    BufferStateFlags initial_state = state_list.front();
    BufferStateFlags final_state = state_list.back();
    if ((initial_state & final_state) == initial_state) { continue; }
    state_list.push_back(initial_state);
    user_list.push_back(vector<uint32_t>{memory_resource});
  }
  return std::make_tuple(buffer_state_list, buffer_user_pass_list);
}
static auto CreateNodeDistanceMapInSameCommandQueueType(const uint32_t pass_num, const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_map<uint32_t, int32_t>> node_distance_map{memory_resource};
  unordered_map<CommandQueueType, uint32_t> last_pass_index_per_command_queue_type{memory_resource_work};
  last_pass_index_per_command_queue_type.reserve(kCommandQueueTypeNum);
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto [it, result] = node_distance_map.insert_or_assign(pass_index, unordered_map<uint32_t, int32_t>{memory_resource});
    auto& distance_map = it->second;
    distance_map.insert_or_assign(pass_index, 0);
    // insert info from previous pass in same queue type
    const auto& command_queue_type = render_pass_command_queue_type_list[pass_index];
    if (last_pass_index_per_command_queue_type.contains(command_queue_type)) {
      const auto& src_distance_map = node_distance_map.at(last_pass_index_per_command_queue_type.at(command_queue_type));
      distance_map.reserve(distance_map.size() + src_distance_map.size());
      for (const auto& [src_index, src_distance] : src_distance_map) {
        distance_map.insert_or_assign(src_index, src_distance - 1);
      }
    }
    last_pass_index_per_command_queue_type.insert_or_assign(command_queue_type, pass_index);
  }
  return node_distance_map;
}
static auto AddNodeDistanceInReverseOrder(unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>&& node_distance_map) {
  for (const auto& [root_index, distance_map] : node_distance_map) {
    for (const auto& [pass_index, distance] : distance_map) {
      if (node_distance_map.at(pass_index).contains(root_index)) { continue; }
      node_distance_map.at(pass_index).insert_or_assign(root_index, -distance);
    }
  }
  return std::move(node_distance_map);
}
static auto AppendInterQueueNodeDistanceMap(const unordered_map<uint32_t, unordered_set<uint32_t>>& inter_queue_pass_dependency, unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>&& node_distance_map) {
  for (const auto& [consumer_pass, producers] : inter_queue_pass_dependency) {
    auto&& distance_map = node_distance_map.at(consumer_pass);
    for (const auto& producer : producers) {
      if (!distance_map.contains(producer)) {
        distance_map.insert_or_assign(producer, -1);
        for (const auto& [descendant, distance] : distance_map) {
          if (distance <= 0) { continue; } // i.e. ancestor or self
          if (!node_distance_map.at(descendant).contains(producer)) {
            node_distance_map.at(descendant).insert_or_assign(producer, - distance - 1);
          }
        }
      }
    }
  }
  return std::move(node_distance_map);
}
static auto ConfigureInterQueuePassDependency(const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_set<uint32_t>> inter_queue_pass_dependency{memory_resource};
  unordered_map<CommandQueueType, uint32_t> src_pass_index_map{memory_resource_work};
  src_pass_index_map.reserve(kCommandQueueTypeNum);
  unordered_map<CommandQueueType, uint32_t> dst_pass_index_map{memory_resource_work};
  dst_pass_index_map.reserve(kCommandQueueTypeNum);
  for (const auto& [buffer_id, buffer_user_list] : buffer_user_pass_list) {
    auto user_pass_list_num = static_cast<uint32_t>(buffer_user_list.size());
    for (uint32_t user_pass_list_index = 0; user_pass_list_index < user_pass_list_num - 1; user_pass_list_index++) {
      if (buffer_user_list[user_pass_list_index].empty()) { continue; }
      src_pass_index_map.clear();
      dst_pass_index_map.clear();
      const auto& src_user_pass_list = buffer_user_list[user_pass_list_index];
      for (const auto& src_pass_index : src_user_pass_list) {
        const auto& src_command_queue_type = render_pass_command_queue_type_list[src_pass_index];
        if (src_pass_index_map.contains(src_command_queue_type) && src_pass_index_map.at(src_command_queue_type) > src_pass_index) { continue; }
        src_pass_index_map.insert_or_assign(src_command_queue_type, src_pass_index);
      }
      const auto& dst_user_pass_list = buffer_user_list[user_pass_list_index + 1];
      for (const auto& dst_pass_index : dst_user_pass_list) {
        const auto& dst_command_queue_type = render_pass_command_queue_type_list[dst_pass_index];
        if (dst_pass_index_map.contains(dst_command_queue_type) && dst_pass_index_map.at(dst_command_queue_type) < dst_pass_index) { continue; }
        dst_pass_index_map.insert_or_assign(dst_command_queue_type, dst_pass_index);
      }
      for (const auto& [src_command_queue_type, src_pass_index] : src_pass_index_map) {
        for (const auto& [dst_command_queue_type, dst_pass_index] : dst_pass_index_map) {
          if (src_command_queue_type == dst_command_queue_type) { continue; }
          bool insert_new_pass = true;
          if (inter_queue_pass_dependency.contains(dst_pass_index)) {
            for (const auto& pass_index : inter_queue_pass_dependency.at(dst_pass_index)) {
              if (render_pass_command_queue_type_list[pass_index] != dst_command_queue_type) { continue; }
              insert_new_pass = (pass_index > src_pass_index);
              break;
            }
          } else {
            inter_queue_pass_dependency.insert_or_assign(dst_pass_index, unordered_set<uint32_t>{memory_resource});
          }
          if (insert_new_pass) {
            inter_queue_pass_dependency.at(dst_pass_index).insert(src_pass_index);
          }
        }
      }
    }
  }
  return inter_queue_pass_dependency;
}
static auto ConfigureTripleInterQueueDependency(const uint32_t pass_num, unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>&& node_distance_map) {
  for (uint32_t ancestor = 0; ancestor < pass_num; ancestor++) {
    auto& ancestor_map = node_distance_map.at(ancestor);
    for (const auto& [middle_pass, distance_to_middle_pass] : ancestor_map) {
      if (distance_to_middle_pass <= 0) { continue; }
      const auto& middle_map = node_distance_map.at(middle_pass);
      for (uint32_t descendant = middle_pass + 1; descendant < pass_num; descendant++) {
        if (ancestor_map.contains(descendant)) { continue; }
        if (!middle_map.contains(descendant)) { continue; }
        if (middle_map.at(descendant) <= 0) { continue; }
        ancestor_map.insert_or_assign(descendant, distance_to_middle_pass + middle_map.at(descendant));
      }
    }
  }
  return std::move(node_distance_map);
}
static auto RemoveRedundantDependencyFromSameQueuePredecessors(const uint32_t pass_num, const vector<CommandQueueType>& render_pass_command_queue_type_list, unordered_map<uint32_t, unordered_set<uint32_t>>&& inter_queue_pass_dependency, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<CommandQueueType, unordered_map<CommandQueueType, uint32_t>> processed_pass_per_queue{memory_resource_work};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    if (!inter_queue_pass_dependency.contains(pass_index)) { continue; }
    const auto& command_queue_type = render_pass_command_queue_type_list[pass_index];
    if (!processed_pass_per_queue.contains(command_queue_type)) {
      processed_pass_per_queue.insert_or_assign(command_queue_type, unordered_map<CommandQueueType, uint32_t>{memory_resource_work});
    }
    auto& processed_pass_list = processed_pass_per_queue.at(command_queue_type);
    auto& dependant_pass_list = inter_queue_pass_dependency.at(pass_index);
    auto it = dependant_pass_list.begin();
    while (it != dependant_pass_list.end()) {
      const auto& dependant_pass = *it;
      const auto& dependant_pass_command_queue_type = render_pass_command_queue_type_list[dependant_pass];
      if (processed_pass_list.contains(dependant_pass_command_queue_type) && processed_pass_list.at(dependant_pass_command_queue_type) >= dependant_pass) {
        it = dependant_pass_list.erase(it);
      } else {
        processed_pass_list.insert_or_assign(dependant_pass_command_queue_type, dependant_pass);
        it++;
      }
    }
    if (dependant_pass_list.empty()) {
      inter_queue_pass_dependency.erase(pass_index);
    }
  }
  return std::move(inter_queue_pass_dependency);
}
enum CommandQueueTypeFlags : uint8_t {
  kCommandQueueTypeFlagsGraphics        = 0x01,
  kCommandQueueTypeFlagsCompute         = 0x02,
  kCommandQueueTypeFlagsTransfer        = 0x04,
  kCommandQueueTypeFlagsGraphicsCompute = kCommandQueueTypeFlagsGraphics | kCommandQueueTypeFlagsCompute,
  kCommandQueueTypeFlagsAll             = kCommandQueueTypeFlagsGraphicsCompute | kCommandQueueTypeFlagsTransfer,
};
static auto GetBufferStateValidCommandQueueTypeFlags(const BufferStateFlags state) {
  if ((state & ~(kBufferStateFlagCopySrc | kBufferStateFlagCopyDst | kBufferStateFlagCommon)) == 0U) { return kCommandQueueTypeFlagsAll; }
  if ((state & kBufferStateFlagSrvPsOnly) != 0U) { return kCommandQueueTypeFlagsGraphics; }
  if ((state & kBufferStateFlagRtv) != 0U) { return kCommandQueueTypeFlagsGraphics; }
  if ((state & kBufferStateFlagDsv) != 0U) { return kCommandQueueTypeFlagsGraphics; }
  return kCommandQueueTypeFlagsGraphicsCompute;
}
static auto FindClosestCommonDescendant(const vector<uint32_t>& ancestors, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, const vector<CommandQueueTypeFlags>& render_pass_command_queue_type_list, const CommandQueueTypeFlags valid_queues) {
  if (ancestors.empty()) {
    // return pass without ancestor
    const uint32_t invalid_pass = ~0U;
    uint32_t retval = invalid_pass;
    for (const auto& [pass_index, distance_map] : node_distance_map) {
      if (pass_index > retval) { continue; }
      if ((render_pass_command_queue_type_list[pass_index] & valid_queues) == 0) { continue; }
      if (std::find_if(distance_map.begin(), distance_map.end(), [](const auto& pair) { return pair.second < 0; }) == distance_map.end()) {
        retval = pass_index;
      }
    }
    if (retval != invalid_pass) {
      return retval;
    }
    for (uint32_t i = 0; i < render_pass_command_queue_type_list.size(); i++) {
      if ((render_pass_command_queue_type_list[i] & valid_queues) != 0) { return i; }
    }
    logerror("valid pass not found in FindClosestCommonDescendant {}", valid_queues);
    return invalid_pass;
  }
  if (ancestors.size() == 1 && ((render_pass_command_queue_type_list[ancestors[0]] & valid_queues) != 0)) { return ancestors[0]; }
  // find closest pass to all ancestors
  const auto& cand_map = node_distance_map.at(ancestors[0]);
  int32_t min_distance = std::numeric_limits<int>::max();
  uint32_t ret_pass = ~0U;
  for (const auto& [cand_pass, distance] : cand_map) {
    if (distance > min_distance) { continue; }
    if ((render_pass_command_queue_type_list[cand_pass] & valid_queues) == 0) { continue; }
    bool valid = true;
    for (uint32_t i = 1; i < ancestors.size(); i++) {
      const auto& current_distance_map = node_distance_map.at(ancestors[i]);
      if (!current_distance_map.contains(cand_pass) || current_distance_map.at(cand_pass) < 0) {
        valid = false;
        break;
      }
    }
    if (!valid) { continue; }
    ret_pass = cand_pass;
    min_distance = distance;
  }
  return ret_pass;
}
static auto FindClosestCommonAncestor(const vector<uint32_t>& descendants, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, const vector<CommandQueueTypeFlags>& render_pass_command_queue_type_list, const CommandQueueTypeFlags valid_queues) {
  if (descendants.empty()) {
    // return pass without descendants
    const uint32_t invalid_pass = ~0U;
    uint32_t retval = invalid_pass;
    for (const auto& [pass_index, distance_map] : node_distance_map) {
      if (pass_index > retval) { continue; }
      if ((render_pass_command_queue_type_list[pass_index] & valid_queues) == 0) { continue; }
      if (std::find_if(distance_map.begin(), distance_map.end(), [](const auto& pair) { return pair.second > 0; }) == distance_map.end()) {
        retval = pass_index;
      }
    }
    if (retval != invalid_pass) {
      return retval;
    }
    const auto len = static_cast<uint32_t>(render_pass_command_queue_type_list.size());
    for (uint32_t i = len - 1; i < len/*i is unsigned*/; i--) {
      if ((render_pass_command_queue_type_list[i] & valid_queues) != 0) { return i; }
    }
    logerror("valid pass not found in FindClosestCommonAncestor {}", valid_queues);
    return invalid_pass;
  }
  if (descendants.size() == 1 && ((render_pass_command_queue_type_list[descendants[0]] & valid_queues) != 0)) {
    return descendants[0];
  }
  // find closest pass to all descendants
  const auto& cand_map = node_distance_map.at(descendants[0]);
  int32_t min_distance = std::numeric_limits<int>::min();
  uint32_t ret_pass = ~0U;
  for (const auto& [cand_pass, distance] : cand_map) {
    if (distance > 0) { continue; }
    if (distance < min_distance) { continue; }
    if ((render_pass_command_queue_type_list[cand_pass] & valid_queues) == 0) { continue; }
    bool valid = true;
    for (uint32_t i = 1; i < descendants.size(); i++) {
      const auto& current_distance_map = node_distance_map.at(descendants[i]);
      if (!current_distance_map.contains(cand_pass) || current_distance_map.at(cand_pass) > 0) {
        valid = false;
        break;
      }
    }
    if (!valid) { continue; }
    ret_pass = cand_pass;
    min_distance = distance;
  }
  return ret_pass;
}
constexpr auto ConvertCommandQueueTypeToFlag(const CommandQueueType& type) {
  switch (type) {
    case CommandQueueType::kGraphics: return kCommandQueueTypeFlagsGraphics;
    case CommandQueueType::kCompute:  return kCommandQueueTypeFlagsCompute;
    case CommandQueueType::kTransfer: return kCommandQueueTypeFlagsTransfer;
  }
  return kCommandQueueTypeFlagsGraphics;
}
static auto ConvertToCommandQueueTypeFlagsList(const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource) {
  auto num = static_cast<uint32_t>(render_pass_command_queue_type_list.size());
  vector<CommandQueueTypeFlags> retval{memory_resource};
  retval.resize(num);
  for (uint32_t i = 0; i < num; i++) {
    retval[i] = ConvertCommandQueueTypeToFlag(render_pass_command_queue_type_list[i]);
  }
  return retval;
}
static auto CreateBufferStateChangeInfoList(const vector<CommandQueueTypeFlags>& render_pass_command_queue_type_flag_list, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, const unordered_map<BufferId, vector<BufferStateFlags>>& buffer_state_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, vector<BufferStateChangeInfo>> buffer_state_change_info_list_map{memory_resource};
  for (const auto& [buffer_id, current_buffer_state_list] : buffer_state_list) {
    buffer_state_change_info_list_map.insert_or_assign(buffer_id, vector<BufferStateChangeInfo>{memory_resource});
    auto& current_buffer_state_change_info_list = buffer_state_change_info_list_map.at(buffer_id);
    const auto& current_buffer_user_pass_list = buffer_user_pass_list.at(buffer_id);
    const auto buffer_state_num = static_cast<uint32_t>(current_buffer_user_pass_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_state_num - 1; buffer_index++) {
      const auto& current_state = current_buffer_state_list[buffer_index];
      const auto& next_state = current_buffer_state_list[buffer_index + 1];
      auto valid_command_queue_type_flag = static_cast<CommandQueueTypeFlags>(GetBufferStateValidCommandQueueTypeFlags(current_state) & GetBufferStateValidCommandQueueTypeFlags(next_state));
      const auto& current_users = current_buffer_user_pass_list[buffer_index];
      const auto& next_users = current_buffer_user_pass_list[buffer_index + 1];
      auto begin_pass = FindClosestCommonDescendant(current_users, node_distance_map, render_pass_command_queue_type_flag_list, valid_command_queue_type_flag);
      auto end_pass = FindClosestCommonAncestor(next_users, node_distance_map, render_pass_command_queue_type_flag_list, valid_command_queue_type_flag);
      current_buffer_state_change_info_list.push_back({
          .barrier_begin_pass_index = begin_pass,
          .barrier_end_pass_index = end_pass,
          .state_before = current_state,
          .state_after = next_state,
          .barrier_begin_pass_pos_type = core::IsContaining(current_users, begin_pass) ? BarrierPosType::kPostPass : BarrierPosType::kPrePass,
          .barrier_end_pass_pos_type = core::IsContaining(next_users, end_pass) ? BarrierPosType::kPrePass : BarrierPosType::kPostPass,
        });
      auto&& current_buffer_state_change_info = current_buffer_state_change_info_list.back();
      if (current_buffer_state_change_info.barrier_begin_pass_index + 1 != current_buffer_state_change_info.barrier_end_pass_index) { continue; }
      if (current_buffer_state_change_info.barrier_begin_pass_pos_type != BarrierPosType::kPostPass) { continue; }
      if (current_buffer_state_change_info.barrier_end_pass_pos_type != BarrierPosType::kPrePass) { continue; }
      current_buffer_state_change_info.barrier_begin_pass_index++;
      current_buffer_state_change_info.barrier_begin_pass_pos_type = current_buffer_state_change_info.barrier_end_pass_pos_type;
    }
  }
  return buffer_state_change_info_list_map;
}
static auto CreateBufferIdNameMap(const uint32_t pass_num, const uint32_t buffer_num, const RenderPassBufferStateList& render_pass_buffer_state_list_with_buffer_name, const vector<vector<BufferId>>& render_pass_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, StrId> buffer_id_name_map{memory_resource};
  buffer_id_name_map.reserve(buffer_num);
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto pass_buffer_num = static_cast<uint32_t>(render_pass_buffer_state_list_with_buffer_name[pass_index].size());
    for (uint32_t buffer_index = 0; buffer_index < pass_buffer_num; buffer_index++) {
      if (!buffer_id_name_map.contains(render_pass_buffer_id_list[pass_index][buffer_index])) {
        buffer_id_name_map.insert_or_assign(render_pass_buffer_id_list[pass_index][buffer_index], render_pass_buffer_state_list_with_buffer_name[pass_index][buffer_index].buffer_name);
      }
    }
  }
  return buffer_id_name_map;
}
static auto CreateBufferConfigList(const vector<BufferId>& buffer_id_list,
                                   const unordered_map<BufferId, StrId>& buffer_id_name_map,
                                   const uint32_t primary_buffer_width, const uint32_t primary_buffer_height,
                                   const uint32_t swapchain_buffer_width, const uint32_t swapchain_buffer_height,
                                   const unordered_map<StrId, BufferSizeInfo>& buffer_size_info_list,
                                   const unordered_map<BufferId, vector<BufferStateFlags>>& buffer_state_list,
                                   const unordered_map<StrId, ClearValue>& buffer_default_clear_value_list,
                                   const unordered_map<StrId, BufferDimensionType>& buffer_dimension_type_list,
                                   const unordered_map<StrId, BufferFormat>& buffer_format_list,
                                   const unordered_map<StrId, DepthStencilFlag>& buffer_depth_stencil_flag_list,
                                   std::pmr::memory_resource* memory_resource) {
  unordered_map<uint32_t, BufferConfig> buffer_config_list{memory_resource};
  buffer_config_list.reserve(buffer_id_list.size());
  for (const auto& buffer_id : buffer_id_list) {
    auto [it, result] = buffer_config_list.insert_or_assign(buffer_id, BufferConfig{});
    auto& config = it->second;
    const auto& buffer_name = buffer_id_name_map.at(buffer_id);
    if (buffer_size_info_list.contains(buffer_name)) {
      const auto& info = buffer_size_info_list.at(buffer_name);
      config.width = GetPhysicalBufferSize(info.type, info.width, primary_buffer_width, swapchain_buffer_width);
      config.height = GetPhysicalBufferSize(info.type, info.height, primary_buffer_height, swapchain_buffer_height);
    } else {
      config.width = primary_buffer_width;
      config.height = primary_buffer_height;
    }
    {
      const auto& current_buffer_state_list = buffer_state_list.at(buffer_id);
      config.initial_state_flags = current_buffer_state_list.front();
      for (const auto& state : current_buffer_state_list) {
        config.state_flags = MergeBufferStateFlags(config.state_flags, state);
      }
    }
    if (buffer_default_clear_value_list.contains(buffer_name)) {
      config.clear_value = buffer_default_clear_value_list.at(buffer_name);
    } else {
      if ((config.state_flags & kBufferStateFlagRtv) != 0U) {
        config.clear_value = GetClearValueDefaultColorBuffer();
      } else if ((config.state_flags & kBufferStateFlagDsv) != 0U) {
        config.clear_value = GetClearValueDefaultDepthBuffer();
      }
    }
    config.dimension = buffer_dimension_type_list.contains(buffer_name) ? buffer_dimension_type_list.at(buffer_name) : BufferDimensionType::k2d;
    if (buffer_format_list.contains(buffer_name)) {
      config.format = buffer_format_list.at(buffer_name);
    } else {
      config.format = (config.state_flags & kBufferStateFlagDsv) != 0U ? BufferFormat::kD32Float : BufferFormat::kR8G8B8A8Unorm;
    }
    config.depth_stencil_flag = buffer_depth_stencil_flag_list.contains(buffer_name) ? buffer_depth_stencil_flag_list.at(buffer_name) : DepthStencilFlag::kDefault;
  }
  return buffer_config_list;
}
static auto ConfigureBarriers(const uint32_t render_pass_num, const vector<BufferId>& buffer_id_list, const unordered_map<BufferId, vector<BufferStateChangeInfo>>& buffer_state_change_info_list_map, std::pmr::memory_resource* memory_resource_barriers) {
  vector<vector<BarrierConfig>> barriers_pre_pass{memory_resource_barriers};
  barriers_pre_pass.resize(render_pass_num);
  vector<vector<BarrierConfig>> barriers_post_pass{memory_resource_barriers};
  barriers_post_pass.resize(render_pass_num);
  for (const auto& buffer_id : buffer_id_list) {
    const auto& buffer_state_change_info_list = buffer_state_change_info_list_map.at(buffer_id);
    for (const auto& buffer_state_change_info : buffer_state_change_info_list) {
      BarrierConfig barrier{
        .buffer_id = buffer_id,
        .split_type = BarrierSplitType::kNone,
      };
      bool disable_split = false;
      if (((buffer_state_change_info.state_before & kBufferStateFlagUav) != 0U) && ((buffer_state_change_info.state_after & kBufferStateFlagUav) != 0U)) {
        if ((buffer_state_change_info.state_before == kBufferStateFlagUavWrite && ((buffer_state_change_info.state_before & kBufferStateReadFlag) == 0U) &&
             buffer_state_change_info.state_after == kBufferStateFlagUavRead && ((buffer_state_change_info.state_after & kBufferStateWriteFlag) == 0U)) ||
            (buffer_state_change_info.state_before == kBufferStateFlagUavRead && ((buffer_state_change_info.state_before & kBufferStateWriteFlag) == 0U) &&
             buffer_state_change_info.state_after == kBufferStateFlagUavWrite && ((buffer_state_change_info.state_after & kBufferStateReadFlag) == 0U))) {
          barrier.params = BarrierUav{};
          disable_split = true;
        } else {
          continue;
        }
      } else {
        barrier.params = BarrierTransition{
          .state_before = buffer_state_change_info.state_before,
          .state_after  = buffer_state_change_info.state_after,
        };
      }
      if (disable_split ||
          (buffer_state_change_info.barrier_begin_pass_index == buffer_state_change_info.barrier_end_pass_index &&
           buffer_state_change_info.barrier_begin_pass_pos_type == buffer_state_change_info.barrier_end_pass_pos_type)) {
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
  return std::make_tuple(barriers_pre_pass, barriers_post_pass);
}
static auto ConfigureQueueSignals(const uint32_t pass_num, const unordered_map<uint32_t, unordered_set<uint32_t>>& inter_queue_pass_dependency, const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource_signals, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_set<uint32_t>> queue_signal{memory_resource_signals};
  for (const auto& [dst_pass_index, src_pass_list] : inter_queue_pass_dependency) {
    for (const auto& src_pass_index : src_pass_list) {
      if (!queue_signal.contains(src_pass_index)) {
        queue_signal.insert_or_assign(src_pass_index, unordered_set<uint32_t>{memory_resource_signals});
      }
      queue_signal.at(src_pass_index).insert(dst_pass_index);
    }
  }
  unordered_map<CommandQueueType, uint32_t> first_pass_per_queue{memory_resource_work};
  unordered_map<CommandQueueType, uint32_t> last_pass_per_queue{memory_resource_work};
  for (uint32_t i = 0; i < pass_num; i++) {
    const auto& command_queue_type = render_pass_command_queue_type_list[i];
    if (!first_pass_per_queue.contains(command_queue_type)) {
      first_pass_per_queue.insert_or_assign(command_queue_type, i);
    }
    last_pass_per_queue.insert_or_assign(command_queue_type, i);
  }
  {
    auto last_pass_it = last_pass_per_queue.begin();
    while (last_pass_it != last_pass_per_queue.end()) {
      if (queue_signal.contains(last_pass_it->second)) {
        last_pass_it = last_pass_per_queue.erase(last_pass_it);
      } else {
        last_pass_it++;
      }
    }
  }
  for (const auto& [src_command_queue_type, src_pass] : last_pass_per_queue) {
    for (const auto& [dst_command_queue_type, dst_pass] : first_pass_per_queue) {
      if (src_command_queue_type == dst_command_queue_type) { continue; }
      if (!queue_signal.contains(src_pass)) {
        queue_signal.insert_or_assign(src_pass, unordered_set<uint32_t>(memory_resource_signals));
      }
      queue_signal.at(src_pass).insert(dst_pass);
    }
  }
  return queue_signal;
}
constexpr auto IsMergeableForBufferCreationImpl(const BufferStateFlags& a, const BufferStateFlags& b) {
  if ((a & kBufferStateFlagRtv) != 0U) {
    if ((b & kBufferStateFlagDsv) != 0U) {
      return false;
    }
  }
  if ((a & kBufferStateFlagDsv) != 0U) {
    if ((b & kBufferStateFlagCbvUpload) != 0U) {
      return false;
    }
  }
  return true;
}
constexpr auto IsMergeableForBufferCreation(const BufferStateFlags& a, const BufferStateFlags& b) {
  if (a == b) { return true; }
  if (!IsMergeableForBufferCreationImpl(a, b)) {
    return false;
  }
  if (!IsMergeableForBufferCreationImpl(b, a)) {
    return false;
  }
  return true;
}
constexpr auto IsBufferConfigMergeable(const BufferConfig& a, const BufferConfig& b) {
  if (a.width != b.width) {
    return false;
  }
  if (a.height != b.height) {
    return false;
  }
  if (a.dimension != b.dimension) {
    return false;
  }
  if (a.format != b.format) {
    return false;
  }
  if (!IsMergeableForBufferCreation(a.state_flags, b.state_flags)) {
    return false;
  }
  return true;
}
static auto UpdateBufferIdListUsingReusableBuffers(const unordered_map<BufferId, BufferId>& reusable_buffers, vector<BufferId>&& buffer_id_list) {
  auto new_end = std::remove_if(buffer_id_list.begin(), buffer_id_list.end(), [&reusable_buffers](const auto& id) { return reusable_buffers.contains(id); });
  buffer_id_list.erase(new_end, buffer_id_list.end());
  return std::move(buffer_id_list);
}
static auto UpdateBufferIdListUsingUsedBuffers(const unordered_set<BufferId>& used_buffers, vector<BufferId>&& buffer_id_list) {
  auto new_end = std::remove_if(buffer_id_list.begin(), buffer_id_list.end(), [&used_buffers](const auto& id) { return !used_buffers.contains(id); });
  buffer_id_list.erase(new_end, buffer_id_list.end());
  return std::move(buffer_id_list);
}
static auto UpdateRenderPassBufferIdListUsingReusableBuffers(const unordered_map<BufferId, BufferId>& reusable_buffers, vector<vector<BufferId>>&& render_pass_buffer_id_list) {
  for (auto&& buffer_list : render_pass_buffer_id_list) {
    auto len = static_cast<uint32_t>(buffer_list.size());
    for (uint32_t i = 0; i < len; i++) {
      if (reusable_buffers.contains(buffer_list[i])) {
        buffer_list[i] = reusable_buffers.at(buffer_list[i]);
      }
    }
  }
  return std::move(render_pass_buffer_id_list);
}
static auto UpdateBufferStateListAndUserPassListUsingReusableBuffers(const unordered_map<BufferId, BufferId>& reusable_buffers, unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list) {
  for (const auto& [original_id, new_id] : reusable_buffers) {
    auto&& src_state = buffer_state_list.at(original_id);
    auto&& dst_state = buffer_state_list.at(new_id);
    auto&& src_user_pass = buffer_user_pass_list.at(original_id);
    auto&& dst_user_pass = buffer_user_pass_list.at(new_id);
    auto dst_state_it = dst_state.begin();
    auto dst_user_pass_it = dst_user_pass.begin();
    while (dst_user_pass_it != dst_user_pass.end() && dst_user_pass_it->front() < src_user_pass.front().front()) {
      dst_user_pass_it++;
      dst_state_it++;
    }
    dst_state.insert(dst_state_it, std::make_move_iterator(src_state.begin()), std::make_move_iterator(src_state.end()));
    dst_user_pass.insert(dst_user_pass_it, std::make_move_iterator(src_user_pass.begin()), std::make_move_iterator(src_user_pass.end()));
    buffer_state_list.erase(original_id);
    buffer_user_pass_list.erase(original_id);
  }
  return std::make_tuple(std::move(buffer_state_list), std::move(buffer_user_pass_list));
}
static auto MergeReusedBufferConfigs(const unordered_map<BufferId, BufferId>& reusable_buffers, unordered_map<BufferId, BufferConfig>&& buffer_config_list) {
  for (const auto& [original_id, new_id] : reusable_buffers) {
    const auto& original_flag = buffer_config_list.at(original_id).state_flags;
    auto& new_flag = buffer_config_list.at(new_id).state_flags;
    new_flag = MergeBufferStateFlags(new_flag, original_flag);
    buffer_config_list.erase(original_id);
  }
  return std::move(buffer_config_list);
}
static auto GatherBufferIdsFromBufferNames(const unordered_map<StrId, unordered_set<BufferId>>& buffer_name_id_map, const unordered_set<StrId>& external_buffer_names, std::pmr::memory_resource* memory_resource) {
  unordered_set<BufferId> external_buffer_id_list{memory_resource};
  external_buffer_id_list.reserve(external_buffer_names.size());
  for (const auto& buffer_name : external_buffer_names) {
    for (const auto& buffer_id : buffer_name_id_map.at(buffer_name)) {
      external_buffer_id_list.insert(buffer_id);
    }
  }
  return external_buffer_id_list;
}
static auto ConfigureRenderPassNewAndExpiredBuffers(const uint32_t pass_num, const vector<uint32_t>& buffer_id_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  vector<vector<uint32_t>> render_pass_new_buffer_list{memory_resource};
  vector<vector<uint32_t>> render_pass_expired_buffer_list{memory_resource};
  render_pass_new_buffer_list.reserve(pass_num);
  render_pass_expired_buffer_list.reserve(pass_num);
  for (uint32_t i = 0; i < pass_num; i++) {
    render_pass_new_buffer_list.push_back(vector<uint32_t>{memory_resource});
    render_pass_expired_buffer_list.push_back(vector<uint32_t>{memory_resource});
  }
  for (const auto& buffer_id : buffer_id_list) {
    const auto& list_list = buffer_user_pass_list.at(buffer_id);
    uint32_t min = pass_num;
    uint32_t max = 0;
    for (const auto& list : list_list) {
      for (const auto& pass : list) {
        if (pass < min) {
          min = pass;
        }
        if (pass > max) {
          max = pass;
        }
      }
    }
    render_pass_new_buffer_list[min].push_back(buffer_id);
    render_pass_expired_buffer_list[max].push_back(buffer_id);
  }
  return std::make_tuple(render_pass_new_buffer_list, render_pass_expired_buffer_list);
}
static constexpr auto AlignAddress(const uint32_t addr, const uint32_t align) {
  const auto mask = align - 1;
  return (addr + mask) & ~mask;
}
static auto RetainMemory(const uint32_t size_in_bytes, const uint32_t alignment_in_bytes, vector<uint32_t>* used_range_start, vector<uint32_t>* used_range_end) {
  auto range_num = static_cast<uint32_t>(used_range_start->size());
  for (uint32_t range_index = 0; range_index < range_num; range_index++) {
    auto addr = (range_index > 0) ? AlignAddress((*used_range_end)[range_index - 1], alignment_in_bytes) : 0;
    logtrace("RetainMemory index:{} addr:{} end:{},{}", range_index, addr, addr + size_in_bytes, (*used_range_start)[range_index]);
    if (addr + size_in_bytes > (*used_range_start)[range_index]) { continue; }
    used_range_start->insert(used_range_start->begin() + range_index, addr);
    used_range_end->insert(used_range_end->begin() + range_index, addr + size_in_bytes);
    logtrace("memory retained");
    return addr;
  }
  uint32_t addr = (range_num == 0) ? 0 : AlignAddress(used_range_end->back(), alignment_in_bytes);
  used_range_start->push_back(addr);
  used_range_end->push_back(addr + size_in_bytes);
  logtrace("memory appended");
  return addr;
}
static void RetainSpecificMemory(const uint32_t addr, const uint32_t size_in_bytes, vector<uint32_t>* used_range_start, vector<uint32_t>* used_range_end) {
  auto range_num = static_cast<uint32_t>(used_range_start->size());
  for (uint32_t range_index = 0; range_index < range_num; range_index++) {
    if ((*used_range_start)[range_index] < addr) { continue; }
    if ((*used_range_start)[range_index] < addr + size_in_bytes) {
      if ((*used_range_start)[range_index] != addr) {
        used_range_start->insert(used_range_start->begin() + range_index, addr);
        used_range_end->insert(used_range_end->begin() + range_index, (*used_range_start)[range_index]);
        range_index++;
      }
      if ((*used_range_end)[range_index] < addr + size_in_bytes) {
        (*used_range_end)[range_index] = addr + size_in_bytes;
      }
      logtrace("RetainSpecificMemory region collided");
      return;
    }
    ASSERT((range_index < range_num - 1) ? (addr + size < (*used_range_start)[range_index + 1]) : true);
    logtrace("memory retained");
    used_range_start->insert(used_range_start->begin() + range_index, addr);
    used_range_end->insert(used_range_end->begin() + range_index, addr + size_in_bytes);
    return;
  }
  used_range_start->push_back(addr);
  used_range_end->push_back(addr + size_in_bytes);
  logtrace("memory appended");
}
static void ReturnMemory(const uint32_t start_addr, vector<uint32_t>* used_range_start, vector<uint32_t>* used_range_end) {
  auto range_num = static_cast<uint32_t>(used_range_start->size());
  for (uint32_t range_index = 0; range_index < range_num; range_index++) {
    auto start = (*used_range_start)[range_index];
    if (start == start_addr) {
      used_range_start->erase(used_range_start->begin() + range_index);
      used_range_end->erase(used_range_end->begin() + range_index);
      return;
    }
    if (start > start_addr) { return; }
  }
}
static auto FindConcurrentPass(const uint32_t pass_num, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, std::pmr::memory_resource* memory_resource) {
  vector<vector<uint32_t>> concurrent_pass_list{memory_resource};
  for (uint32_t src = 0; src < pass_num; src++) {
    concurrent_pass_list.push_back(vector<uint32_t>{memory_resource});
    const auto& distance_map = node_distance_map.at(src);
    for (uint32_t dst = 0; dst < pass_num; dst++) {
      if (distance_map.contains(dst)) { continue; }
      concurrent_pass_list.back().push_back(dst);
    }
  }
  return concurrent_pass_list;
}
static auto FindConcurrentBuffers(const vector<vector<uint32_t>>& concurrent_pass_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  vector<unordered_set<BufferId>> concurrent_buffer_list{memory_resource};
  for (const auto& pass_list : concurrent_pass_list) {
    concurrent_buffer_list.push_back(unordered_set<BufferId>{memory_resource});
    for (const auto& pass : pass_list) {
      for (const auto& buffer_id : render_pass_buffer_id_list[pass]) {
        concurrent_buffer_list.back().insert(buffer_id);
      }
    }
  }
  return concurrent_buffer_list;
}
static auto ConfigureBufferAddressOffset(const uint32_t pass_num, const vector<unordered_set<BufferId>>& concurrent_buffer_list, const vector<vector<uint32_t>>& render_pass_new_buffer_list, const vector<vector<uint32_t>>& render_pass_expired_buffer_list, const unordered_map<BufferId, BufferConfig>& buffer_config_list, const unordered_map<BufferId, uint32_t>& buffer_size_list, const unordered_map<BufferId, uint32_t>& buffer_alignment_list, const unordered_set<BufferId>& excluded_buffers, const bool buffer_reuse_enabled, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<BufferId, uint32_t> buffer_address_offset_list{memory_resource};
  buffer_address_offset_list.reserve(buffer_config_list.size());
  unordered_map<BufferId, BufferId> renamed_buffers{memory_resource};
  unordered_map<uint32_t, vector<BufferId>> render_pass_before_memory_aliasing_list{memory_resource};
  unordered_map<uint32_t, vector<BufferId>> render_pass_after_memory_aliasing_list{memory_resource};
  vector<uint32_t> used_range_start{memory_resource_work};
  vector<uint32_t> used_range_end{memory_resource_work};
  for (const auto& new_buffer : render_pass_new_buffer_list[0]) {
    if (excluded_buffers.contains(new_buffer)) { continue; }
    buffer_address_offset_list.insert_or_assign(new_buffer, RetainMemory(buffer_size_list.at(new_buffer), buffer_alignment_list.at(new_buffer), &used_range_start, &used_range_end));
    logtrace("new pass:{} id:{} addr:{} size:{} align:{}", 0, new_buffer, buffer_address_offset_list.at(new_buffer), buffer_size_list.at(new_buffer), buffer_alignment_list.at(new_buffer));
  }
  unordered_set<BufferId> processed_new_buffer_list{memory_resource_work};
  vector<uint32_t> memory_reusable_buffer_list{memory_resource_work};
  vector<uint32_t> memory_reusable_buffer_start_addr_list{memory_resource_work};
  vector<uint32_t> memory_reusable_buffer_end_addr_list{memory_resource_work};
  vector<uint32_t> used_range_including_concurrent_buffers_start{memory_resource_work};
  vector<uint32_t> used_range_including_concurrent_buffers_end{memory_resource_work};
  for (uint32_t pass_index = 1; pass_index < pass_num; pass_index++) {
    // release expired buffers
    if (buffer_reuse_enabled) {
      for (const auto& expired_buffer : render_pass_expired_buffer_list[pass_index-1]) {
        if (excluded_buffers.contains(expired_buffer)) { continue; }
        auto addr = buffer_address_offset_list.at(expired_buffer);
        ReturnMemory(addr, &used_range_start, &used_range_end);
        bool append_addr = true;
        for (auto it = memory_reusable_buffer_list.begin(); it != memory_reusable_buffer_list.end(); it++) {
          const auto& comp_buffer = *it;
          const auto& comp_buffer_addr = buffer_address_offset_list.at(comp_buffer);
          if (comp_buffer_addr < addr) { continue; }
          auto distance = it - memory_reusable_buffer_list.begin();
          memory_reusable_buffer_list.insert(it, expired_buffer);
          memory_reusable_buffer_start_addr_list.insert(memory_reusable_buffer_start_addr_list.begin() + distance, addr);
          memory_reusable_buffer_end_addr_list.insert(memory_reusable_buffer_end_addr_list.begin() + distance, addr + buffer_size_list.at(expired_buffer));
          append_addr = false;
          logtrace("exp pass:{} id:{} addr:{} size:{} align:{}", pass_index - 1, expired_buffer, buffer_address_offset_list.at(expired_buffer), buffer_size_list.at(expired_buffer), buffer_alignment_list.at(expired_buffer));
          break;
        }
        if (append_addr) {
          memory_reusable_buffer_list.push_back(expired_buffer);
          memory_reusable_buffer_start_addr_list.push_back(addr);
          memory_reusable_buffer_end_addr_list.push_back(addr + buffer_size_list.at(expired_buffer));
          logtrace("exp pass:{} id:{} addr:{} size:{} align:{}", pass_index - 1, expired_buffer, buffer_address_offset_list.at(expired_buffer), buffer_size_list.at(expired_buffer), buffer_alignment_list.at(expired_buffer));
        }
      }
    }
    for (uint32_t i = 0; i < memory_reusable_buffer_list.size(); i++) {
      logtrace("reusable mem2 id:{} start:{} end:{} size:{}", memory_reusable_buffer_list[i], memory_reusable_buffer_start_addr_list[i], memory_reusable_buffer_end_addr_list[i], memory_reusable_buffer_end_addr_list[i] - memory_reusable_buffer_start_addr_list[i]);
    }
    // reuse buffers
    for (const auto& new_buffer : render_pass_new_buffer_list[pass_index]) {
      if (excluded_buffers.contains(new_buffer)) { continue; }
      const auto& buffer_config = buffer_config_list.at(new_buffer);
      for (auto it = memory_reusable_buffer_list.begin(); it != memory_reusable_buffer_list.end(); it++) {
        const auto& expired_buffer = *it;
        if (concurrent_buffer_list[pass_index].contains(expired_buffer)) { continue; }
        if (!IsBufferConfigMergeable(buffer_config, buffer_config_list.at(expired_buffer))) { continue; }
        auto addr = buffer_address_offset_list.at(expired_buffer);
        auto distance = it - memory_reusable_buffer_list.begin();
        if (addr != *(memory_reusable_buffer_start_addr_list.begin() + distance)) { continue; }
        auto& buffer_size = buffer_size_list.at(new_buffer);
        if (addr + buffer_size != *(memory_reusable_buffer_end_addr_list.begin() + distance)) { continue; }
        RetainSpecificMemory(addr, buffer_size, &used_range_start, &used_range_end);
        buffer_address_offset_list.insert_or_assign(new_buffer, addr);
        renamed_buffers.insert_or_assign(new_buffer, expired_buffer);
        logtrace("reused {}->{} addr:{}", expired_buffer, new_buffer, addr);
        memory_reusable_buffer_list.erase(it);
        memory_reusable_buffer_start_addr_list.erase(memory_reusable_buffer_start_addr_list.begin() + distance);
        memory_reusable_buffer_end_addr_list.erase(memory_reusable_buffer_end_addr_list.begin() + distance);
        processed_new_buffer_list.insert(new_buffer);
        break;
      }
    }
    for (uint32_t i = 0; i < memory_reusable_buffer_list.size(); i++) {
      logtrace("reusable mem1 id:{} start:{} end:{} size:{}", memory_reusable_buffer_list[i], memory_reusable_buffer_start_addr_list[i], memory_reusable_buffer_end_addr_list[i], memory_reusable_buffer_end_addr_list[i] - memory_reusable_buffer_start_addr_list[i]);
    }
    // mark ranges used by concurrent buffers
    used_range_including_concurrent_buffers_start.assign(used_range_start.begin(), used_range_start.end());
    used_range_including_concurrent_buffers_end.assign(used_range_end.begin(), used_range_end.end());
    for (const auto& buffer_id : concurrent_buffer_list[pass_index]) {
      if (!buffer_address_offset_list.contains(buffer_id)) { continue; }
      RetainSpecificMemory(buffer_address_offset_list.at(buffer_id), buffer_size_list.at(buffer_id), &used_range_including_concurrent_buffers_start, &used_range_including_concurrent_buffers_end);
    }
    for (uint32_t i = 0; i < used_range_including_concurrent_buffers_start.size(); i++) {
      logtrace("used region concurrent {} {} {}", i, used_range_including_concurrent_buffers_start[i], used_range_including_concurrent_buffers_end[i]);
    }
    // allocate new buffers
    for (const auto& new_buffer : render_pass_new_buffer_list[pass_index]) {
      if (excluded_buffers.contains(new_buffer)) { continue; }
      if (processed_new_buffer_list.contains(new_buffer)) { continue; }
      const auto& buffer_size = buffer_size_list.at(new_buffer);
      auto addr = RetainMemory(buffer_size, buffer_alignment_list.at(new_buffer), &used_range_including_concurrent_buffers_start, &used_range_including_concurrent_buffers_end);
      RetainSpecificMemory(addr, buffer_size, &used_range_start, &used_range_end);
      buffer_address_offset_list.insert_or_assign(new_buffer, addr);
      logtrace("new pass:{} id:{} addr:{} size:{} align:{}", pass_index, new_buffer, addr, buffer_size, buffer_alignment_list.at(new_buffer));
      // check for memory aliasing
      auto it = memory_reusable_buffer_list.begin();
      while (it != memory_reusable_buffer_list.end()) {
        const auto& expired_buffer = *it;
        const auto reusable_buffer_index = static_cast<uint32_t>(it - memory_reusable_buffer_list.begin());
        const auto expired_region_start = memory_reusable_buffer_start_addr_list[reusable_buffer_index];
        if (addr + buffer_size <= expired_region_start) {
          it++;
          logtrace("no aliasing A {}->{} index:{} start-addr:{}", expired_buffer, new_buffer, reusable_buffer_index, expired_region_start);
          continue;
        }
        const auto expired_region_end = memory_reusable_buffer_end_addr_list[reusable_buffer_index];
        if (addr >= expired_region_end) {
          it++;
          logtrace("no aliasing B {}->{} index:{} end-addr:{}", expired_buffer, new_buffer, reusable_buffer_index, expired_region_end);
          continue;
        }
        if (!render_pass_before_memory_aliasing_list.contains(pass_index)) {
          render_pass_before_memory_aliasing_list.insert_or_assign(pass_index, vector<BufferId>{memory_resource});
          render_pass_after_memory_aliasing_list.insert_or_assign(pass_index, vector<BufferId>{memory_resource});
        }
        render_pass_before_memory_aliasing_list.at(pass_index).push_back(expired_buffer);
        render_pass_after_memory_aliasing_list.at(pass_index).push_back(new_buffer);
        logtrace("mem alias {}->{}", expired_buffer, new_buffer);
        if (addr <= expired_region_start && addr + buffer_size >= expired_region_end) {
          logtrace("mem reuse removed {}", *it);
          it = memory_reusable_buffer_list.erase(it);
          memory_reusable_buffer_start_addr_list.erase(memory_reusable_buffer_start_addr_list.begin() + reusable_buffer_index);
          memory_reusable_buffer_end_addr_list.erase(memory_reusable_buffer_end_addr_list.begin() + reusable_buffer_index);
        } else {
          if (addr <= expired_region_start) {
            logtrace("mem reuse start update {} -> {}", memory_reusable_buffer_start_addr_list[reusable_buffer_index], addr + buffer_size);
            memory_reusable_buffer_start_addr_list[reusable_buffer_index] = addr + buffer_size;
          } else {
            if (addr + buffer_size < expired_region_end) {
              auto region_end = memory_reusable_buffer_end_addr_list[reusable_buffer_index];
              logtrace("mem reuse start,end end-update {} -> {}", memory_reusable_buffer_end_addr_list[reusable_buffer_index], addr);
              logtrace("mem reuse start,end region-added start:{} end{}", addr + buffer_size, region_end);
              memory_reusable_buffer_end_addr_list[reusable_buffer_index] = addr;
              it = memory_reusable_buffer_list.insert(it, expired_buffer);
              memory_reusable_buffer_start_addr_list.insert(memory_reusable_buffer_start_addr_list.begin() + reusable_buffer_index, addr + buffer_size);
              memory_reusable_buffer_end_addr_list.insert(memory_reusable_buffer_end_addr_list.begin() + reusable_buffer_index, region_end);
            } else {
              logtrace("mem reuse end update {}->{}", memory_reusable_buffer_end_addr_list[reusable_buffer_index], addr);
              memory_reusable_buffer_end_addr_list[reusable_buffer_index] = addr;
            }
          }
          it++;
        }
      }
    }
    processed_new_buffer_list.clear();
    for (uint32_t i = 0; i < memory_reusable_buffer_list.size(); i++) {
      logtrace("reusable mem3 id:{} start:{} end:{} size:{}", memory_reusable_buffer_list[i], memory_reusable_buffer_start_addr_list[i], memory_reusable_buffer_end_addr_list[i], memory_reusable_buffer_end_addr_list[i] - memory_reusable_buffer_start_addr_list[i]);
    }
  }
  return std::make_tuple(buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list);
}
static auto GetBufferSizeInfo(const unordered_map<BufferId, BufferConfig>& buffer_config_list, const std::function<std::tuple<uint32_t, uint32_t>(const BufferConfig&)>& configure_function, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, uint32_t> buffer_size_list{memory_resource};
  unordered_map<BufferId, uint32_t> buffer_alignment_list{memory_resource};
  for (auto [buffer_id, buffer_config] : buffer_config_list) {
    auto [size, alignment] = configure_function(buffer_config);
    buffer_size_list.insert_or_assign(buffer_id, size);
    buffer_alignment_list.insert_or_assign(buffer_id, alignment);
  }
  return std::make_tuple(buffer_size_list, buffer_alignment_list);
}
static auto MergeRenamedBufferDuplicativeBufferIds(const vector<BufferId>& buffer_id_list, unordered_map<BufferId, BufferId>&& renamed_buffers) {
  for (const auto& buffer_id : buffer_id_list) {
    if (!renamed_buffers.contains(buffer_id)) { continue; }
    const auto& new_id = renamed_buffers.at(buffer_id);
    if (!renamed_buffers.contains(new_id)) { continue; }
    renamed_buffers.at(buffer_id) = renamed_buffers.at(new_id);
  }
  return std::move(renamed_buffers);
}
static auto CalculateNewPassIndexAfterPassCulling(const uint32_t pass_num, const unordered_set<uint32_t>& used_render_pass_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<uint32_t, uint32_t> new_render_pass_index_list{memory_resource};
  uint32_t new_pass_index = 0;
  for (uint32_t original_pass_index = 0; original_pass_index < pass_num; original_pass_index++) {
    if (!used_render_pass_list.contains(original_pass_index)) { continue; }
    if (original_pass_index != new_pass_index) {
      new_render_pass_index_list.insert_or_assign(original_pass_index, new_pass_index);
    }
    new_pass_index++;
  }
  return new_render_pass_index_list;
}
static auto CullUsedRenderPass(const unordered_map<BufferId, vector<BufferStateFlags>>& buffer_state_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, const vector<vector<BufferStateFlags>>& render_pass_buffer_state_flag_list, unordered_set<BufferId>&& required_buffers, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_set<uint32_t> used_pass_list{memory_resource};
  unordered_set<BufferId> used_buffer_list{memory_resource};
  unordered_set<uint32_t> required_pass_list{memory_resource_work};
  while (!required_buffers.empty()) {
    const auto required_buffer = *required_buffers.begin();
    required_buffers.erase(required_buffer);
    used_buffer_list.insert(required_buffer);
    const auto& required_buffer_state_list = buffer_state_list.at(required_buffer);
    const auto& producer_pass_cands = buffer_user_pass_list.at(required_buffer);
    const auto state_num = static_cast<uint32_t>(required_buffer_state_list.size());
    for (uint32_t state_index = 0; state_index < state_num; state_index++) {
      if (IsBufferInWritableState(required_buffer_state_list[state_index])) {
        for (const auto& producer_cand : producer_pass_cands[state_index]) {
          required_pass_list.insert(producer_cand);
        }
      }
    }
    while (!required_pass_list.empty()) {
      const auto required_pass = *required_pass_list.begin();
      required_pass_list.erase(required_pass);
      used_pass_list.insert(required_pass);
      const auto& consumed_buffer_state_list = render_pass_buffer_state_flag_list[required_pass];
      const auto& consumed_buffer_cands = render_pass_buffer_id_list[required_pass];
      const auto buffer_num = static_cast<uint32_t>(consumed_buffer_cands.size());
      for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
        if (IsBufferInReadableState(consumed_buffer_state_list[buffer_index])) {
          required_buffers.insert(consumed_buffer_cands[buffer_index]);
        }
      }
    }
  }
  return std::make_tuple(used_pass_list, used_buffer_list);
}
static auto UpdateRenderPassBufferInfoWithNewPassIndex(const uint32_t pass_num, const unordered_map<uint32_t, uint32_t>& new_render_pass_index_list, vector<vector<BufferId>>&& render_pass_buffer_id_list, vector<vector<BufferStateFlags>>&& render_pass_buffer_state_flag_list) {
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    if (!new_render_pass_index_list.contains(pass_index)) { continue; }
    render_pass_buffer_id_list[new_render_pass_index_list.at(pass_index)] = std::move(render_pass_buffer_id_list[pass_index]);
    render_pass_buffer_state_flag_list[new_render_pass_index_list.at(pass_index)] = std::move(render_pass_buffer_state_flag_list[pass_index]);
  }
  render_pass_buffer_id_list.erase(render_pass_buffer_id_list.begin() + pass_num, render_pass_buffer_id_list.end());
  render_pass_buffer_state_flag_list.erase(render_pass_buffer_state_flag_list.begin() + pass_num, render_pass_buffer_state_flag_list.end());
  return std::make_tuple(std::move(render_pass_buffer_id_list), std::move(render_pass_buffer_state_flag_list));
}
static auto UpdateBufferStateInfoWithNewPassIndex(const unordered_set<uint32_t>& used_pass_list, const unordered_set<BufferId>& used_buffer_list, const unordered_map<uint32_t, uint32_t>& new_render_pass_index_list, unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list) {
  std::erase_if(buffer_state_list, [&used_buffer_list](const auto& id) { return !used_buffer_list.contains(id.first); });
  std::erase_if(buffer_user_pass_list, [&used_buffer_list](const auto& id) { return !used_buffer_list.contains(id.first); });
  for (auto&& [buffer_id, user_pass_list] : buffer_user_pass_list) {
    auto pass_list_it = user_pass_list.begin();
    auto& state_list = buffer_state_list.at(buffer_id);
    uint32_t state_count = 0;
    while (pass_list_it != user_pass_list.end()) {
      auto pass_it = pass_list_it->begin();
      while (pass_it != pass_list_it->end()) {
        if (!used_pass_list.contains(*pass_it)) {
          pass_it = pass_list_it->erase(pass_it);
        } else {
          if (new_render_pass_index_list.contains(*pass_it)) {
            *pass_it = new_render_pass_index_list.at(*pass_it);
          }
          pass_it++;
        }
      }
      if (pass_list_it->empty()) {
        pass_list_it = user_pass_list.erase(pass_list_it);
        state_list.erase(state_list.begin() + state_count);
      } else {
        pass_list_it++;
        state_count++;
      }
    }
  }
  auto user_it = buffer_user_pass_list.begin();
  auto state_it = buffer_state_list.begin();
  while (user_it != buffer_user_pass_list.end()) {
    if (user_it->second.empty()) {
      user_it = buffer_user_pass_list.erase(user_it);
      state_it = buffer_state_list.erase(state_it);
    } else {
      user_it++;
      state_it++;
    }
  }
  return std::make_tuple(std::move(buffer_state_list), std::move(buffer_user_pass_list));
}
static auto GetRenderCommandQueueTypeListWithNewPassIndex(const uint32_t pass_num, const unordered_map<uint32_t, uint32_t>& new_render_pass_index_list, const vector<CommandQueueType>& src_list, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, uint32_t> new_index_to_old_index{memory_resource_work};
  new_index_to_old_index.reserve(new_render_pass_index_list.size());
  for (const auto& [old_index, new_index] : new_render_pass_index_list) {
    new_index_to_old_index.insert_or_assign(new_index, old_index);
  }
  vector<CommandQueueType> fixed_list{memory_resource};
  fixed_list.resize(pass_num);
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    if (new_index_to_old_index.contains(pass_index)) {
      fixed_list[pass_index] = src_list[new_index_to_old_index.at(pass_index)];
    } else {
      fixed_list[pass_index] = src_list[pass_index];
    }
  }
  return fixed_list;
}
static auto UpdateMemoryAliasingListWithRenamedBufferIdList(const unordered_map<BufferId, BufferId>& renamed_buffers, unordered_map<uint32_t, vector<BufferId>>&& render_pass_memory_aliasing_list) {
  for (auto&& [pass_index, list] : render_pass_memory_aliasing_list) {
    for (auto&& buffer_id : list) {
      if (renamed_buffers.contains(buffer_id)) {
        buffer_id = renamed_buffers.at(buffer_id);
      }
    }
  }
  return std::move(render_pass_memory_aliasing_list);
}
static auto AppendMemoryAliasingBarriers(const uint32_t pass_num, const unordered_map<uint32_t, vector<BufferId>>& render_pass_before_memory_aliasing_list, const unordered_map<uint32_t, vector<BufferId>>& render_pass_after_memory_aliasing_list, vector<vector<BarrierConfig>>&& barriers_pre_pass) {
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    if (!render_pass_before_memory_aliasing_list.contains(pass_index)) { continue; }
    const auto& before = render_pass_before_memory_aliasing_list.at(pass_index);
    const auto& after = render_pass_after_memory_aliasing_list.at(pass_index);
    const auto num = static_cast<uint32_t>(before.size());
    for (uint32_t buffer_index = 0; buffer_index < num; buffer_index++) {
      barriers_pre_pass[pass_index].push_back(BarrierConfig{
          .buffer_id = before[buffer_index],
          .params = BarrierAliasing{.buffer_id_after_aliasing = after[buffer_index]},
        });
    }
  }
  return std::move(barriers_pre_pass);
}
static auto UpdateRenderPassBufferClearInfoWithRenamedBufferIdList(const unordered_map<BufferId, BufferId>& renamed_buffers, vector<unordered_map<BufferId, BufferClearType>>&& render_pass_buffer_clear_info) {
  for (auto&& map : render_pass_buffer_clear_info) {
    auto it = map.begin();
    while (it != map.end()) {
      if (!renamed_buffers.contains(it->first)) {
        it++;
        continue;
      }
      auto buffer_id = renamed_buffers.at(it->first);
      auto clear_type = it->second;
      map.erase(it);
      map.insert_or_assign(buffer_id, clear_type);
      it = map.begin();
    }
  }
  return std::move(render_pass_buffer_clear_info);
}
static auto AppendBufferClearInfoCausedByMemoryAliasing(const uint32_t pass_num, const unordered_map<uint32_t, vector<BufferId>>& render_pass_after_memory_aliasing_list, vector<unordered_map<BufferId, BufferClearType>>&& render_pass_buffer_clear_info) {
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    if (!render_pass_after_memory_aliasing_list.contains(pass_index)) {
      continue;
    }
    for (auto& buffer_id : render_pass_after_memory_aliasing_list.at(pass_index)) {
      if (render_pass_buffer_clear_info[pass_index].contains(buffer_id)) {
        continue;
      }
      render_pass_buffer_clear_info[pass_index].insert_or_assign(buffer_id, BufferClearType::kDontCare);
    }
  }
  return std::move(render_pass_buffer_clear_info);
}
static auto AddBufferClearInfoForInitialWrite(const unordered_map<BufferId, vector<BufferStateFlags>>& buffer_state_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, vector<unordered_map<BufferId, BufferClearType>>&& render_pass_buffer_clear_info) {
  for (auto& [buffer_id, pass_list_list] : buffer_user_pass_list) {
    auto& state_list = buffer_state_list.at(buffer_id);
    if (pass_list_list[0].empty()) { continue; }
    if (IsBufferInReadableState(state_list[0])) { continue; }
    bool already_cleared = false;
    for (auto& pass_index : pass_list_list[0]) {
      if (render_pass_buffer_clear_info[pass_index].contains(buffer_id)) {
        already_cleared = true;
        break;
      }
    }
    if (!already_cleared) {
      // might need to find the ancestor pass of all the other passes in pass_list_list[0]
      render_pass_buffer_clear_info[pass_list_list[0][0]].insert_or_assign(buffer_id, BufferClearType::kDontCare);
    }
  }
  return std::move(render_pass_buffer_clear_info);
}
static auto RemoveDisabledCommandQueueType(const unordered_set<CommandQueueType>& disabled_command_queue_type_list, vector<CommandQueueType>&& render_pass_command_queue_type_list) {
  for (auto&& queue : render_pass_command_queue_type_list) {
    if (disabled_command_queue_type_list.contains(queue)) {
      queue = CommandQueueType::kGraphics;
    }
  }
  return std::move(render_pass_command_queue_type_list);
}
static auto GetAsyncGroupInfo(const unordered_map<StrId, uint32_t>& render_pass_id_map, const unordered_map<StrId, unordered_set<StrId>>& async_compute_group_with_name, const unordered_map<StrId, unordered_set<CommandQueueType>>& preceding_command_queue_type_list_with_name, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  vector<unordered_set<uint32_t>> async_compute_group{memory_resource};
  async_compute_group.reserve(async_compute_group_with_name.size());
  vector<unordered_set<CommandQueueType>> preceding_command_queue_type_list{memory_resource};
  preceding_command_queue_type_list.reserve(preceding_command_queue_type_list_with_name.size());
  vector<uint32_t> min_pass_index_list{memory_resource_work};
  min_pass_index_list.reserve(async_compute_group_with_name.size());
  for (auto& [group_name, pass_names] : async_compute_group_with_name) {
    uint32_t min_pass_index = std::numeric_limits<uint32_t>::max();
    for (auto& pass_name : pass_names) {
      auto& pass_index = render_pass_id_map.at(pass_name);
      if (pass_index < min_pass_index) {
        min_pass_index = pass_index;
      }
    }
    uint32_t index = 0;
    while (index < min_pass_index_list.size()) {
      if (min_pass_index <= min_pass_index_list[index]) break;
      index++;
    }
    if (index >= min_pass_index_list.size()) {
      async_compute_group.push_back(unordered_set<uint32_t>{memory_resource});
      preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{memory_resource});
      min_pass_index_list.push_back(min_pass_index);
    } else {
      async_compute_group.insert(async_compute_group.begin() + index, unordered_set<uint32_t>{memory_resource});
      preceding_command_queue_type_list.insert(preceding_command_queue_type_list.begin() + index, unordered_set<CommandQueueType>{memory_resource});
      min_pass_index_list.insert(min_pass_index_list.begin() + index, min_pass_index);
    }
    for (auto& pass_name : pass_names) {
      async_compute_group[index].insert(render_pass_id_map.at(pass_name));
    }
    if (preceding_command_queue_type_list_with_name.contains(group_name)) {
      for (auto& queue : preceding_command_queue_type_list_with_name.at(group_name)) {
        preceding_command_queue_type_list[index].insert(queue);
      }
    }
  }
  return std::make_tuple(async_compute_group, preceding_command_queue_type_list);
}
static auto GetRenderPassOrderPerQueue(const uint32_t pass_num, const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<CommandQueueType, vector<uint32_t>> render_pass_order_per_command_queue_type{memory_resource};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& queue = render_pass_command_queue_type_list[pass_index];
    if (!render_pass_order_per_command_queue_type.contains(queue)) {
      render_pass_order_per_command_queue_type.insert_or_assign(queue, vector<uint32_t>{memory_resource});
    }
    render_pass_order_per_command_queue_type.at(queue).push_back(pass_index);
  }
  return render_pass_order_per_command_queue_type;
}
static auto ConfigureAsyncComputeGroupPerQueueType(const unordered_map<CommandQueueType, vector<uint32_t>>& render_pass_order_per_command_queue_type, const vector<unordered_set<uint32_t>>& async_compute_group, std::pmr::memory_resource* memory_resource) {
  vector<unordered_map<CommandQueueType, vector<uint32_t>>> pre_group_pass{memory_resource}, group_pass{memory_resource};
  const auto group_num = static_cast<uint32_t>(async_compute_group.size());
  pre_group_pass.reserve(group_num + 1);
  group_pass.reserve(group_num);
  for (uint32_t i = 0; i <= group_num; i++) {
    pre_group_pass.push_back(unordered_map<CommandQueueType, vector<uint32_t>>{memory_resource});
    if (i < group_num) {
      group_pass.push_back(unordered_map<CommandQueueType, vector<uint32_t>>{memory_resource});
    }
    for (const auto& [queue, list] : render_pass_order_per_command_queue_type) {
      pre_group_pass.back().insert_or_assign(queue, vector<uint32_t>{memory_resource});
      if (i < group_num) {
        group_pass.back().insert_or_assign(queue, vector<uint32_t>{memory_resource});
      }
    }
  }
  for (const auto& [queue, pass_order] : render_pass_order_per_command_queue_type) {
    uint32_t current_group_index = 0;
    uint32_t processed_pass_index = 0;
    while (processed_pass_index < pass_order.size()) {
      uint32_t next_group_index = current_group_index;
      uint32_t next_in_group_pass_index = processed_pass_index;
      while (next_group_index < async_compute_group.size()) {
        while (next_in_group_pass_index < pass_order.size() && !async_compute_group[next_group_index].contains(pass_order[next_in_group_pass_index])) {
          next_in_group_pass_index++;
        }
        if (next_in_group_pass_index < pass_order.size()) { break; }
        next_group_index++;
        next_in_group_pass_index = processed_pass_index;
      }
      if (next_group_index == async_compute_group.size()) {
        next_in_group_pass_index = static_cast<uint32_t>(pass_order.size());
      }
      if (processed_pass_index < next_in_group_pass_index) {
        auto& dst = pre_group_pass[current_group_index].at(queue);
        dst.reserve(dst.size() + next_in_group_pass_index - processed_pass_index);
        dst.insert(dst.end(), pass_order.begin() + processed_pass_index, pass_order.begin() + next_in_group_pass_index);
      }
      if (next_group_index == async_compute_group.size()) {
        break;
      }
      processed_pass_index = next_in_group_pass_index;
      current_group_index = next_group_index;
      while (processed_pass_index < pass_order.size() && async_compute_group[current_group_index].contains(pass_order[processed_pass_index])) {
        group_pass[current_group_index].at(queue).push_back(pass_order[processed_pass_index]);
        processed_pass_index++;
      }
      current_group_index++;
    }
  }
  return std::make_tuple(pre_group_pass, group_pass);
}
static auto GetUsedCommandQueueList(const unordered_map<CommandQueueType, vector<uint32_t>>& map, std::pmr::memory_resource* memory_resource) {
  vector<CommandQueueType> used_command_queues{memory_resource};
  if (map.contains(CommandQueueType::kGraphics)) {
    used_command_queues.push_back(CommandQueueType::kGraphics);
  }
  if (map.contains(CommandQueueType::kCompute)) {
    used_command_queues.push_back(CommandQueueType::kCompute);
  }
  if (map.contains(CommandQueueType::kTransfer)) {
    used_command_queues.push_back(CommandQueueType::kTransfer);
  }
  return used_command_queues;
}
static auto ConfigureAsyncComputeGroupRenderPassOrder(const vector<CommandQueueType>& used_command_queues, const vector<unordered_map<CommandQueueType, vector<uint32_t>>>& pre_group_pass, const vector<unordered_map<CommandQueueType, vector<uint32_t>>>& group_pass, const vector<unordered_set<CommandQueueType>>& preceding_command_queue_type_list, std::pmr::memory_resource* memory_resource) {
  vector<uint32_t> render_pass_order_initial_frame{memory_resource};
  vector<uint32_t> render_pass_order_succeeding_frame{memory_resource};
  const auto group_num = static_cast<uint32_t>(group_pass.size());
  for (uint32_t group_index = 0; group_index <= group_num; group_index++) {
    for (const auto& queue : used_command_queues) {
      for (const auto& pass_index : pre_group_pass[group_index].at(queue)) {
        render_pass_order_succeeding_frame.push_back(pass_index);
      }
    }
    if (group_index == group_num) { break; }
    for (const auto& queue : used_command_queues) {
      const auto is_in_preceding_frame = preceding_command_queue_type_list[group_index].contains(queue);
      for (const auto& pass_index : group_pass[group_index].at(queue)) {
        if (is_in_preceding_frame) {
          render_pass_order_initial_frame.push_back(pass_index);
        }
        render_pass_order_succeeding_frame.push_back(pass_index);
      }
    }
  }
  return std::make_tuple(render_pass_order_initial_frame, render_pass_order_succeeding_frame);
}
static auto ConfigureAsyncComputeGroupPassSignals(const vector<unordered_map<CommandQueueType, vector<uint32_t>>>& pre_group_pass, const vector<unordered_map<CommandQueueType, vector<uint32_t>>>& group_pass, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_set<uint32_t>> pass_signal_info_succeeding_frame{memory_resource};
  unordered_map<CommandQueueType, uint32_t> signal_pass_per_queue{memory_resource_work};
  const auto group_num = static_cast<uint32_t>(group_pass.size());
  for (uint32_t group_index = 0; group_index < group_num; group_index++) {
    signal_pass_per_queue.clear();
    for (const auto& [signal_queue, signal_pass_list] : pre_group_pass[group_index]) {
      if (signal_pass_list.empty() || group_pass[group_index].at(signal_queue).empty()) { continue; }
      signal_pass_per_queue.insert_or_assign(signal_queue, signal_pass_list.back());
    }
    if (group_index > 0) {
      for (const auto& [signal_queue, signal_pass_list] : group_pass[group_index - 1]) {
        if (signal_pass_list.empty() || signal_pass_per_queue.contains(signal_queue) || group_pass[group_index].at(signal_queue).empty()) { continue; }
        signal_pass_per_queue.insert_or_assign(signal_queue, signal_pass_list.back());
      }
    }
    for (const auto& [signal_queue, signal_pass] : signal_pass_per_queue) {
      for (const auto& [wait_queue, wait_pass_list] : group_pass[group_index]) {
        if (wait_pass_list.empty()) { continue; }
        if (signal_queue == wait_queue) { continue; }
        if (!pass_signal_info_succeeding_frame.contains(signal_pass)) {
          pass_signal_info_succeeding_frame.insert_or_assign(signal_pass, unordered_set<uint32_t>{memory_resource});
        }
        pass_signal_info_succeeding_frame.at(signal_pass).insert(wait_pass_list.front());
      }
    }
  }
  return pass_signal_info_succeeding_frame;
}
static auto ConfigureFrameSyncSignalInfo(const unordered_map<CommandQueueType, vector<uint32_t>>& render_pass_order_per_command_queue_type, unordered_map<uint32_t, unordered_set<uint32_t>>&& pass_signal_info_succeeding_frame, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<CommandQueueType, uint32_t> signal_pass_list{memory_resource_work};
  unordered_map<CommandQueueType, uint32_t> wait_pass_list{memory_resource_work};
  for (const auto& [queue, pass_list] : render_pass_order_per_command_queue_type) {
    if (pass_list.empty()) { continue; }
    if (!pass_signal_info_succeeding_frame.contains(pass_list.back())) {
      signal_pass_list.insert_or_assign(queue, pass_list.back());
    }
    bool insert_to_wait_pass_list = true;
    for (const auto& [signal_pass, signal_waiting_pass_list] : pass_signal_info_succeeding_frame) {
      if (signal_waiting_pass_list.contains(pass_list.front())) {
        insert_to_wait_pass_list = false;
        break;
      }
    }
    if (insert_to_wait_pass_list) {
      wait_pass_list.insert_or_assign(queue, pass_list.front());
    }
  }
  for (const auto& [signal_queue, signal_pass] : signal_pass_list) {
    for (const auto& [wait_queue, wait_pass] : wait_pass_list) {
      if (signal_queue == wait_queue) { continue; }
      if (!pass_signal_info_succeeding_frame.contains(signal_pass)) {
        pass_signal_info_succeeding_frame.insert_or_assign(signal_pass, unordered_set<uint32_t>{memory_resource});
      }
      pass_signal_info_succeeding_frame.at(signal_pass).insert(wait_pass);
    }
  }
  return std::move(pass_signal_info_succeeding_frame);
}
static auto ConfigurePassSignalsForPrecedingFrame(const vector<unordered_map<CommandQueueType, vector<uint32_t>>>& group_pass, const vector<unordered_set<CommandQueueType>>& preceding_command_queue_type_list, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_set<uint32_t>> pass_signal_info_initial_frame{memory_resource};
  unordered_map<CommandQueueType, uint32_t> signal_pass_per_queue{memory_resource};
  unordered_map<CommandQueueType, uint32_t> waiting_pass_per_queue{memory_resource_work};
  const auto group_num = static_cast<uint32_t>(group_pass.size());
  for (uint32_t group_index = 0; group_index < group_num; group_index++) {
    if (preceding_command_queue_type_list[group_index].empty()) { continue; }
    if (!signal_pass_per_queue.empty()) {
      for (const auto& [queue, pass_list] : group_pass[group_index]) {
        if (!pass_list.empty() && preceding_command_queue_type_list[group_index].contains(queue)) {
          waiting_pass_per_queue.insert_or_assign(queue, pass_list.front());
        }
      }
      if (!waiting_pass_per_queue.empty()) {
        for (const auto& [signal_queue, signal_pass] : signal_pass_per_queue) {
          for (const auto& [queue, pass] : waiting_pass_per_queue) {
            if (signal_queue == queue) { continue; }
            if (!pass_signal_info_initial_frame.contains(signal_pass)) {
              pass_signal_info_initial_frame.insert_or_assign(signal_pass, unordered_set<uint32_t>{memory_resource});
            }
            pass_signal_info_initial_frame.at(signal_pass).insert(pass);
          }
        }
        signal_pass_per_queue.clear();
        waiting_pass_per_queue.clear();
      }
    }
    for (const auto& [queue, pass_list] : group_pass[group_index]) {
      if (!pass_list.empty() && preceding_command_queue_type_list[group_index].contains(queue)) {
        signal_pass_per_queue.insert_or_assign(queue, pass_list.back());
      }
    }
  }
  return std::make_tuple(pass_signal_info_initial_frame, signal_pass_per_queue);
}
static auto ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(const unordered_map<CommandQueueType, vector<uint32_t>>& render_pass_order_per_command_queue_type, const vector<unordered_map<CommandQueueType, vector<uint32_t>>>& pre_group_pass, const vector<unordered_map<CommandQueueType, vector<uint32_t>>>& group_pass, const unordered_map<CommandQueueType, uint32_t>& unprocessed_signal_pass, unordered_map<uint32_t, unordered_set<uint32_t>>&& pass_signal_info_initial_frame, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  bool is_pre_group_empty = true;
  for (const auto& [queue, list] : pre_group_pass[0]) {
    if (!list.empty()) {
      is_pre_group_empty = false;
      break;
    }
  }
  unordered_map<CommandQueueType, uint32_t> waiting_pass_per_queue{memory_resource_work};
  for (const auto& [queue, pass_list] : render_pass_order_per_command_queue_type) {
    if (pass_list.empty()) { continue; }
    if ((is_pre_group_empty && !group_pass[0].at(queue).empty()) || (!is_pre_group_empty && !pre_group_pass[0].at(queue).empty())) {
        waiting_pass_per_queue.insert_or_assign(queue, pass_list.front());
      }
  }
  for (const auto& [signal_queue, signal_pass] : unprocessed_signal_pass) {
    for (const auto& [wait_queue, wait_pass] : waiting_pass_per_queue) {
      if (signal_queue == wait_queue) { continue; }
      if (!pass_signal_info_initial_frame.contains(signal_pass)) {
        pass_signal_info_initial_frame.insert_or_assign(signal_pass, unordered_set<uint32_t>{memory_resource});
      }
      pass_signal_info_initial_frame.at(signal_pass).insert(wait_pass);
    }
  }
  return std::move(pass_signal_info_initial_frame);
}
static auto DisposeProcessedFrameIndex(const uint32_t processed_index, vector<uint32_t>&& next_frame_index_list) {
  auto next_frame_index = next_frame_index_list[processed_index];
  if (next_frame_index > processed_index) {
    next_frame_index--;
  }
  next_frame_index_list.erase(next_frame_index_list.begin() + processed_index);
  for (auto&& index : next_frame_index_list) {
    if (index == processed_index) {
      logwarn("DisposeProcessedFrameIndex disposing used index {}", index);
      continue;
    }
    if (index > processed_index) {
      index--;
    }
  }
  return std::make_tuple(next_frame_index, std::move(next_frame_index_list));
}
void RenderGraph::Build(const RenderGraphConfig& config, std::pmr::memory_resource* memory_resource_work) {
  if (config.GetMandatoryBufferNameList().empty()) {
    logwarn("mandatory_buffer_name_list_ is empty");
  }
  if (!config.GetBufferSizeInfoFunction()) {
    logwarn("buffer_size_info_function_ is empty");
  }
  render_pass_num_ = config.GetRenderPassNum();
  std::tie(buffer_id_list_, render_pass_buffer_id_list_, render_pass_buffer_state_flag_list_, render_pass_buffer_clear_info_) = InitBufferIdList(render_pass_num_, config.GetRenderPassBufferStateList(), memory_resource_, memory_resource_work);
  auto buffer_id_name_map = CreateBufferIdNameMap(render_pass_num_, static_cast<uint32_t>(buffer_id_list_.size()), config.GetRenderPassBufferStateList(), render_pass_buffer_id_list_, memory_resource_work);
  auto buffer_name_id_map = GetBufferNameIdMap(render_pass_num_, config.GetRenderPassBufferStateList(), render_pass_buffer_id_list_, memory_resource_work);
  auto required_buffers = GatherBufferIdsFromBufferNames(buffer_name_id_map, config.GetMandatoryBufferNameList(), memory_resource_work);
  auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num_, buffer_id_list_, render_pass_buffer_id_list_, render_pass_buffer_state_flag_list_, memory_resource_work);
  auto [used_pass_list, used_buffer_list] = CullUsedRenderPass(buffer_state_list, buffer_user_pass_list, render_pass_buffer_id_list_, render_pass_buffer_state_flag_list_, std::move(required_buffers), memory_resource_work, memory_resource_work);
  buffer_id_list_ = UpdateBufferIdListUsingUsedBuffers(used_buffer_list, std::move(buffer_id_list_));
#if 0
  render_pass_command_queue_type_list_ = RemoveDisabledCommandQueueType(config.GetDisabledCommandQueueTypeList(), std::move(render_pass_command_queue_type_list_));
  auto [async_compute_group, preceding_command_queue_type_list] = GetAsyncGroupInfo(render_pass_id_map_, config.GetAsyncComputeGroup(), config.GetAsyncComputeGroupPrecedingCommandQueueTypeList(), memory_resource_work, memory_resource_work);
  auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(render_pass_num_, render_pass_command_queue_type_list_, memory_resource_work);
#else
  auto new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num_, used_pass_list, memory_resource_work);
  render_pass_num_ = static_cast<decltype(render_pass_num_)>(used_pass_list.size());
  std::tie(render_pass_buffer_id_list_, render_pass_buffer_state_flag_list_) = UpdateRenderPassBufferInfoWithNewPassIndex(render_pass_num_, new_render_pass_index_list, std::move(render_pass_buffer_id_list_), std::move(render_pass_buffer_state_flag_list_));
  std::tie(buffer_state_list, buffer_user_pass_list) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list), std::move(buffer_user_pass_list));
  render_pass_command_queue_type_list_ = GetRenderCommandQueueTypeListWithNewPassIndex(render_pass_num_, new_render_pass_index_list, config.GetRenderPassCommandQueueTypeList(), memory_resource_, memory_resource_work);
  render_pass_command_queue_type_list_ = RemoveDisabledCommandQueueType(config.GetDisabledCommandQueueTypeList(), std::move(render_pass_command_queue_type_list_));
  auto initial_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, config.GetBufferInitialStateList(), memory_resource_work);
  std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
  auto final_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, config.GetBufferFinalStateList(), memory_resource_work);
  std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work); // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(render_pass_num_, render_pass_command_queue_type_list_, memory_resource_work, memory_resource_work);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list_, memory_resource_work, memory_resource_work); // NOLINT(bugprone-use-after-move,hicpp-invalid-access-moved)
  inter_queue_pass_dependency = RemoveRedundantDependencyFromSameQueuePredecessors(render_pass_num_, render_pass_command_queue_type_list_, std::move(inter_queue_pass_dependency), memory_resource_work);
  queue_signals_ = ConfigureQueueSignals(render_pass_num_, inter_queue_pass_dependency, render_pass_command_queue_type_list_, memory_resource_, memory_resource_work);
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = ConfigureTripleInterQueueDependency(render_pass_num_, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  buffer_config_list_ = CreateBufferConfigList(buffer_id_list_,
                                               buffer_id_name_map,
                                               config.GetPrimaryBufferWidth(), config.GetPrimaryBufferHeight(),
                                               config.GetSwapchainBufferWidth(), config.GetSwapchainBufferHeight(),
                                               config.GetBufferSizeInfoList(),
                                               buffer_state_list, // NOLINT(hicpp-invalid-access-moved,bugprone-use-after-move)
                                               config.GetBufferDefaultClearValueList(),
                                               config.GetBufferDimensionTypeList(),
                                               config.GetBufferFormatList(),
                                               config.GetBufferDepthStencilFlagList(),
                                               memory_resource_);
  auto excluded_buffers = GatherBufferIdsFromBufferNames(buffer_name_id_map, config.GetExternalBufferNameList(), memory_resource_);
  std::tie(buffer_size_list_, buffer_alignment_list_) = GetBufferSizeInfo(buffer_config_list_, config.GetBufferSizeInfoFunction(), memory_resource_work);
  auto [render_pass_new_buffer_list, render_pass_expired_buffer_list] = ConfigureRenderPassNewAndExpiredBuffers(render_pass_num_, buffer_id_list_, buffer_user_pass_list, memory_resource_work);
  auto concurrent_pass_list = FindConcurrentPass(render_pass_num_, node_distance_map, memory_resource_work);
  auto concurrent_buffer_list = FindConcurrentBuffers(concurrent_pass_list, render_pass_buffer_id_list_, memory_resource_work);
  unordered_map<BufferId, BufferId> renamed_buffers{memory_resource_work};
  unordered_map<uint32_t, vector<BufferId>> render_pass_before_memory_aliasing_list{memory_resource_work};
  unordered_map<uint32_t, vector<BufferId>> render_pass_after_memory_aliasing_list{memory_resource_work};
  std::tie(buffer_address_offset_list_, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list) = ConfigureBufferAddressOffset(render_pass_num_, concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list_, buffer_size_list_, buffer_alignment_list_, excluded_buffers, config.IsBufferReuseEnabled(), memory_resource_work, memory_resource_work);
  renamed_buffers = MergeRenamedBufferDuplicativeBufferIds(buffer_id_list_, std::move(renamed_buffers));
  buffer_id_list_ = UpdateBufferIdListUsingReusableBuffers(renamed_buffers, std::move(buffer_id_list_));
  render_pass_buffer_id_list_ = UpdateRenderPassBufferIdListUsingReusableBuffers(renamed_buffers, std::move(render_pass_buffer_id_list_));
  std::tie(buffer_state_list, buffer_user_pass_list) = UpdateBufferStateListAndUserPassListUsingReusableBuffers(renamed_buffers, std::move(buffer_state_list), std::move(buffer_user_pass_list));
  buffer_config_list_ = MergeReusedBufferConfigs(renamed_buffers, std::move(buffer_config_list_));
  render_pass_before_memory_aliasing_list = UpdateMemoryAliasingListWithRenamedBufferIdList(renamed_buffers, std::move(render_pass_before_memory_aliasing_list));
  render_pass_after_memory_aliasing_list = UpdateMemoryAliasingListWithRenamedBufferIdList(renamed_buffers, std::move(render_pass_after_memory_aliasing_list));
  render_pass_buffer_clear_info_ = UpdateRenderPassBufferClearInfoWithRenamedBufferIdList(renamed_buffers, std::move(render_pass_buffer_clear_info_));
  render_pass_buffer_clear_info_ = AppendBufferClearInfoCausedByMemoryAliasing(render_pass_num_, render_pass_after_memory_aliasing_list, std::move(render_pass_buffer_clear_info_));
  render_pass_buffer_clear_info_ = AddBufferClearInfoForInitialWrite(buffer_state_list, buffer_user_pass_list, std::move(render_pass_buffer_clear_info_));
  std::tie(buffer_state_list, buffer_user_pass_list) = RevertBufferStateToInitialState(std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work); // NOLINT(hicpp-invalid-access-moved,bugprone-use-after-move)
  auto render_pass_command_queue_type_flag_list = ConvertToCommandQueueTypeFlagsList(render_pass_command_queue_type_list_, memory_resource_work);
  auto buffer_state_change_info_list_map = CreateBufferStateChangeInfoList(render_pass_command_queue_type_flag_list, node_distance_map, buffer_state_list, buffer_user_pass_list, memory_resource_work); // NOLINT(hicpp-invalid-access-moved,bugprone-use-after-move)
  std::tie(barriers_pre_pass_, barriers_post_pass_) = ConfigureBarriers(render_pass_num_, buffer_id_list_, buffer_state_change_info_list_map, memory_resource_);
  barriers_pre_pass_ = AppendMemoryAliasingBarriers(render_pass_num_, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list, std::move(barriers_pre_pass_));
#endif
}
#ifdef BUILD_WITH_TEST
static auto CollectBufferDataForTest(const vector<vector<BufferId>>& render_pass_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  vector<BufferId> buffer_id_list{memory_resource};
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{memory_resource};
  auto pass_num = static_cast<uint32_t>(render_pass_buffer_id_list.size());
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto buffer_num = static_cast<uint32_t>(render_pass_buffer_id_list[pass_index].size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      const auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      if (!buffer_user_pass_list.contains(buffer_id)) {
        buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{memory_resource});
        buffer_id_list.push_back(buffer_id);
      }
      buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{memory_resource});
      buffer_user_pass_list.at(buffer_id).back().push_back(pass_index);
    }
  }
  return std::make_tuple(buffer_id_list, buffer_user_pass_list);
}
static auto CreateBufferStateListBasedOnBufferUserListForTest(const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, vector<BufferStateFlags>> buffer_state_list{memory_resource};
  buffer_state_list.reserve(buffer_user_pass_list.size());
  for (const auto& [buffer_id, list_list] : buffer_user_pass_list) {
    buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{memory_resource});
    for (uint32_t i = 0; i < list_list.size(); i++) {
      buffer_state_list.at(buffer_id).push_back(i % 2 == 0 ? kBufferStateFlagCopyDst : kBufferStateFlagCopySrc);
    }
  }
  return buffer_state_list;
}
static auto GetBufferSizeAndAlignmentImpl([[maybe_unused]]const illuminate::gfx::BufferConfig& config) {
  return std::make_tuple(16U, 8U); // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
}
static auto CreateRenderPassBufferInfoForTest(const unordered_map<BufferId, vector<BufferStateFlags>>& buffer_state_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  vector<vector<BufferId>> render_pass_buffer_id_list{memory_resource};
  vector<vector<BufferStateFlags>> render_pass_buffer_state_flag_list{memory_resource};
  for (const auto& [buffer_id, buffer_states] : buffer_state_list) {
    const auto& user_pass_list = buffer_user_pass_list.at(buffer_id);
    const auto buffer_num = static_cast<uint32_t>(user_pass_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      for (const auto& pass_index : user_pass_list[buffer_index]) {
        while (render_pass_buffer_id_list.size() <= pass_index) {
          render_pass_buffer_id_list.push_back(vector<BufferId>{memory_resource});
          render_pass_buffer_state_flag_list.push_back(vector<BufferStateFlags>{memory_resource});
        }
        render_pass_buffer_id_list[pass_index].push_back(buffer_id);
        render_pass_buffer_state_flag_list[pass_index].push_back(buffer_states[buffer_index]);
      }
    }
  }
  return std::make_tuple(render_pass_buffer_id_list, render_pass_buffer_state_flag_list);
}
#endif
} // namespace illuminate::gfx
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_offset_in_bytes_persistent = 0;
const uint32_t buffer_size_in_bytes_persistent = 16 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_persistent + buffer_size_in_bytes_persistent;
const uint32_t buffer_size_in_bytes_scene = 16 * 1024;
const uint32_t buffer_offset_in_bytes_frame = buffer_offset_in_bytes_scene + buffer_size_in_bytes_scene;
const uint32_t buffer_size_in_bytes_frame = 4 * 1024;
const uint32_t buffer_offset_in_bytes_work = buffer_offset_in_bytes_frame + buffer_size_in_bytes_frame;
const uint32_t buffer_size_in_bytes_work = 64 * 1024;
std::array<std::byte, buffer_offset_in_bytes_work + buffer_size_in_bytes_work> buffer{};
} // namespace
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include "doctest/doctest.h"
#include <numeric>
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - single pass") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(node_distance_map.size() == 1); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 1); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - two pass (graphics only)") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(node_distance_map.size() == 2); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 1); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(node_distance_map.contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).size() == 2); // NOLINT
  CHECK(node_distance_map.at(1).contains(0)); // NOLINT
  CHECK(node_distance_map.at(1).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.size() == 2); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 2); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(node_distance_map.at(0).contains(1)); // NOLINT
  CHECK(node_distance_map.at(0).at(1) == 1); // NOLINT
  CHECK(node_distance_map.contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).size() == 2); // NOLINT
  CHECK(node_distance_map.at(1).contains(0)); // NOLINT
  CHECK(node_distance_map.at(1).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - two independent pass (graphics+compute)") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(node_distance_map.size() == 2); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 1); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(node_distance_map.contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).size() == 1); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics->compute)") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  unordered_map<uint32_t, unordered_set<uint32_t>> inter_queue_pass_dependency{&memory_resource_work};
  inter_queue_pass_dependency.insert_or_assign(1, unordered_set<uint32_t>{&memory_resource_work});
  inter_queue_pass_dependency.at(1).insert(0);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.size() == 2); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 2); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(node_distance_map.at(0).contains(1)); // NOLINT
  CHECK(node_distance_map.at(0).at(1) == 1); // NOLINT
  CHECK(node_distance_map.contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).size() == 2); // NOLINT
  CHECK(node_distance_map.at(1).contains(0)); // NOLINT
  CHECK(node_distance_map.at(1).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics->compute(+graphics))") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  unordered_map<uint32_t, unordered_set<uint32_t>> inter_queue_pass_dependency{&memory_resource_work};
  inter_queue_pass_dependency.insert_or_assign(2, unordered_set<uint32_t>{&memory_resource_work});
  inter_queue_pass_dependency.at(2).insert(0);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.size() == 3); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 3); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(node_distance_map.at(0).contains(1)); // NOLINT
  CHECK(node_distance_map.at(0).at(1) == 1); // NOLINT
  CHECK(node_distance_map.at(0).contains(2)); // NOLINT
  CHECK(node_distance_map.at(0).at(2) == 1); // NOLINT
  CHECK(node_distance_map.contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).size() == 2); // NOLINT
  CHECK(node_distance_map.at(1).contains(0)); // NOLINT
  CHECK(node_distance_map.at(1).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
  CHECK(node_distance_map.contains(2)); // NOLINT
  CHECK(node_distance_map.at(2).size() == 2); // NOLINT
  CHECK(node_distance_map.at(2).contains(0)); // NOLINT
  CHECK(node_distance_map.at(2).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(2).contains(2)); // NOLINT
  CHECK(node_distance_map.at(2).at(2) == 0); // NOLINT
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics+compute->graphics)") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  unordered_map<uint32_t, unordered_set<uint32_t>> inter_queue_pass_dependency{&memory_resource_work};
  inter_queue_pass_dependency.insert_or_assign(2, unordered_set<uint32_t>{&memory_resource_work});
  inter_queue_pass_dependency.at(2).insert(1);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.size() == 3); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 2); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(node_distance_map.at(0).contains(2)); // NOLINT
  CHECK(node_distance_map.at(0).at(2) == 1); // NOLINT
  CHECK(node_distance_map.contains(2)); // NOLINT
  CHECK(node_distance_map.at(1).size() == 2); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
  CHECK(node_distance_map.at(1).contains(2)); // NOLINT
  CHECK(node_distance_map.at(1).at(2) == 1); // NOLINT
  CHECK(node_distance_map.contains(2)); // NOLINT
  CHECK(node_distance_map.at(2).size() == 3); // NOLINT
  CHECK(node_distance_map.at(2).contains(0)); // NOLINT
  CHECK(node_distance_map.at(2).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(2).contains(1)); // NOLINT
  CHECK(node_distance_map.at(2).at(1) == -1); // NOLINT
  CHECK(node_distance_map.at(2).contains(2)); // NOLINT
  CHECK(node_distance_map.at(2).at(2) == 0); // NOLINT
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics+compute->graphics+compute)") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  unordered_map<uint32_t, unordered_set<uint32_t>> inter_queue_pass_dependency{&memory_resource_work};
  inter_queue_pass_dependency.insert_or_assign(2, unordered_set<uint32_t>{&memory_resource_work});
  inter_queue_pass_dependency.at(2).insert(1);
  inter_queue_pass_dependency.insert_or_assign(3, unordered_set<uint32_t>{&memory_resource_work});
  inter_queue_pass_dependency.at(3).insert(0);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.size() == 5); // NOLINT
  CHECK(node_distance_map.contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).size() == 4); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(node_distance_map.at(0).contains(2)); // NOLINT
  CHECK(node_distance_map.at(0).at(2) == 1); // NOLINT
  CHECK(node_distance_map.at(0).contains(3)); // NOLINT
  CHECK(node_distance_map.at(0).at(3) == 1); // NOLINT
  CHECK(node_distance_map.at(0).contains(4)); // NOLINT
  CHECK(node_distance_map.at(0).at(4) == 2); // NOLINT
  CHECK(node_distance_map.contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).size() == 4); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
  CHECK(node_distance_map.at(1).contains(2)); // NOLINT
  CHECK(node_distance_map.at(1).at(2) == 1); // NOLINT
  CHECK(node_distance_map.at(1).contains(3)); // NOLINT
  CHECK(node_distance_map.at(1).at(3) == 1); // NOLINT
  CHECK(node_distance_map.at(1).contains(4)); // NOLINT
  CHECK(node_distance_map.at(1).at(4) == 2); // NOLINT
  CHECK(node_distance_map.contains(2)); // NOLINT
  CHECK(node_distance_map.at(2).size() == 3); // NOLINT
  CHECK(node_distance_map.at(2).contains(0)); // NOLINT
  CHECK(node_distance_map.at(2).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(2).contains(1)); // NOLINT
  CHECK(node_distance_map.at(2).at(1) == -1); // NOLINT
  CHECK(node_distance_map.at(2).contains(2)); // NOLINT
  CHECK(node_distance_map.at(2).at(2) == 0); // NOLINT
  CHECK(node_distance_map.contains(3)); // NOLINT
  CHECK(node_distance_map.at(3).size() == 4); // NOLINT
  CHECK(node_distance_map.at(3).contains(0)); // NOLINT
  CHECK(node_distance_map.at(3).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(3).contains(1)); // NOLINT
  CHECK(node_distance_map.at(3).at(1) == -1); // NOLINT
  CHECK(node_distance_map.at(3).contains(3)); // NOLINT
  CHECK(node_distance_map.at(3).at(3) == 0); // NOLINT
  CHECK(node_distance_map.at(3).contains(4)); // NOLINT
  CHECK(node_distance_map.at(3).at(4) == 1); // NOLINT
  CHECK(node_distance_map.contains(3)); // NOLINT
  CHECK(node_distance_map.at(4).size() == 4); // NOLINT
  CHECK(node_distance_map.at(4).contains(0)); // NOLINT
  CHECK(node_distance_map.at(4).at(0) == -2); // NOLINT
  CHECK(node_distance_map.at(4).contains(1)); // NOLINT
  CHECK(node_distance_map.at(4).at(1) == -2); // NOLINT
  CHECK(node_distance_map.at(4).contains(3)); // NOLINT
  CHECK(node_distance_map.at(4).at(3) == -1); // NOLINT
  CHECK(node_distance_map.at(4).contains(4)); // NOLINT
  CHECK(node_distance_map.at(4).at(4) == 0); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.empty()); // NOLINT
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(1);
  inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).contains(0)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - multiple buffers") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(1);
  buffer_user_pass_list.insert_or_assign(1, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(0);
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(2);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 2); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).contains(0)); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(2)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).contains(0)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - remove processed dependency from ancestor pass") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(1);
  buffer_user_pass_list.at(0).back().push_back(2);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).contains(0)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - remove processed dependency from ancestor pass with multiple buffers") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(2);
  buffer_user_pass_list.insert_or_assign(1, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(0);
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(1);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  inter_queue_pass_dependency = RemoveRedundantDependencyFromSameQueuePredecessors(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, std::move(inter_queue_pass_dependency), &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).contains(0)); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(2)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - remove processed dependency from ancestor pass with different queue type") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(1);
  buffer_user_pass_list.at(0).back().push_back(3);
  buffer_user_pass_list.insert_or_assign(1, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(1);
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(2);
  buffer_user_pass_list.at(1).back().push_back(4);
  buffer_user_pass_list.insert_or_assign(2, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(2).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(2).back().push_back(2);
  buffer_user_pass_list.at(2).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(2).back().push_back(3);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  inter_queue_pass_dependency = RemoveRedundantDependencyFromSameQueuePredecessors(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, std::move(inter_queue_pass_dependency), &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 2); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).contains(0)); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(3)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(3).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(3).contains(2)); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(0)); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(2)); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(4)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - 3 queue test") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(2);
  buffer_user_pass_list.insert_or_assign(1, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(0);
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(1);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 2); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).contains(0)); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(2)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).contains(0)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - dependent on multiple queue") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(2);
  buffer_user_pass_list.insert_or_assign(1, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(1);
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(2);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(2)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).size() == 2); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).contains(0)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).contains(1)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - dependent on single queue") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(1);
  buffer_user_pass_list.insert_or_assign(1, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(1);
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(2);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 2); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(1).contains(0)); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(2)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).contains(1)); // NOLINT
}
TEST_CASE("ConfigureInterPassDependency - remove same queue dependency") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  SUBCASE("buffer used in different state") {
    buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  }
  buffer_user_pass_list.at(0).back().push_back(1);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.empty()); // NOLINT
}
TEST_CASE("barrier for load from srv") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  render_graph_config.SetBufferSizeInfoFunction(GetBufferSizeAndAlignmentImpl);
  render_graph_config.AddMandatoryBufferName(StrId("swapchain"));
  CHECK(render_graph_config.GetRenderPassNum() == 0); // NOLINT
  auto render_pass_id = render_graph_config.CreateNewRenderPass({.pass_name = StrId("draw"), .command_queue_type = CommandQueueType::kGraphics});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag, BufferClearFlag::kNone});
  CHECK(render_graph_config.GetRenderPassNum() == 1); // NOLINT
  auto prev_render_pass_id = render_pass_id;
  render_pass_id = render_graph_config.CreateNewRenderPass({.pass_name = StrId("copy"), .command_queue_type = CommandQueueType::kGraphics});
  CHECK(render_graph_config.GetRenderPassNum() == 2); // NOLINT
  CHECK(render_pass_id != prev_render_pass_id); // NOLINT
  render_graph_config.AppendRenderPassBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag, BufferClearFlag::kNone});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_id, {StrId("swapchain"),  kBufferStateFlagRtv, kWriteFlag, BufferClearFlag::kNone});
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  {
    // check render graph config
    auto render_pass_num = render_graph_config.GetRenderPassNum();
    auto [buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, render_pass_buffer_clear_info] = InitBufferIdList(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), &memory_resource_work, &memory_resource_work);
    CHECK(buffer_id_list.size() == 2); // NOLINT
    CHECK(buffer_id_list[0] == 0); // NOLINT
    CHECK(buffer_id_list[1] == 1); // NOLINT
    CHECK(render_pass_buffer_id_list.size() == 2); // NOLINT
    CHECK(render_pass_buffer_id_list[0].size() == 1); // NOLINT
    CHECK(render_pass_buffer_id_list[0][0] == buffer_id_list[0]); // NOLINT
    CHECK(render_pass_buffer_id_list[1].size() == 2); // NOLINT
    CHECK(render_pass_buffer_id_list[1][0] == buffer_id_list[0]); // NOLINT
    CHECK(render_pass_buffer_id_list[1][1] == buffer_id_list[1]); // NOLINT
    CHECK(render_pass_buffer_state_flag_list.size() == 2); // NOLINT
    CHECK(render_pass_buffer_state_flag_list[0].size() == 1); // NOLINT
    CHECK(render_pass_buffer_state_flag_list[0][0] == kBufferStateFlagRtv); // NOLINT
    CHECK(render_pass_buffer_state_flag_list[1].size() == 2); // NOLINT
    CHECK(render_pass_buffer_state_flag_list[1][0] == kBufferStateFlagSrvPsOnly); // NOLINT
    CHECK(render_pass_buffer_state_flag_list[1][1] == kBufferStateFlagRtv); // NOLINT
    CHECK(render_pass_buffer_clear_info.size() == 2); // NOLINT
    CHECK(render_pass_buffer_clear_info[0].empty()); // NOLINT
    CHECK(render_pass_buffer_clear_info[1].empty()); // NOLINT
    auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num, buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, &memory_resource_work);
    auto buffer_name_id_map = GetBufferNameIdMap(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, &memory_resource_work);
    auto initial_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferInitialStateList(), &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    auto final_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferFinalStateList(), &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work); // NOLINT
    CHECK(buffer_state_list.size() == 2); // NOLINT
    CHECK(buffer_user_pass_list.size() == 2); // NOLINT
    CHECK(buffer_state_list.contains(0)); // NOLINT
    CHECK(buffer_state_list.at(0).size() == 2); // NOLINT
    CHECK(buffer_state_list.at(0)[0] == kBufferStateFlagRtv); // NOLINT
    CHECK(buffer_state_list.at(0)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
    CHECK(buffer_user_pass_list.contains(0)); // NOLINT
    CHECK(buffer_user_pass_list.at(0).size() == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[0].size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[0][0] == 0); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[1].size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[1][0] == 1); // NOLINT
    CHECK(buffer_state_list.contains(1)); // NOLINT
    CHECK(buffer_state_list.at(1).size() == 3); // NOLINT
    CHECK(buffer_state_list.at(1)[0] == kBufferStateFlagPresent); // NOLINT
    CHECK(buffer_state_list.at(1)[1] == kBufferStateFlagRtv); // NOLINT
    CHECK(buffer_state_list.at(1)[2] == kBufferStateFlagPresent); // NOLINT
    CHECK(buffer_user_pass_list.contains(1)); // NOLINT
    CHECK(buffer_user_pass_list.at(1).size() == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[0].empty()); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[1].size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[1][0] == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[2].empty()); // NOLINT
    memory_resource_work.Reset();
  }
  RenderGraph render_graph(&memory_resource_scene);
  render_graph.Build(render_graph_config, &memory_resource_work);
  CHECK(render_graph.GetRenderPassNum() == 2); // NOLINT
  {
    const auto& buffer_list = render_graph.GetBufferIdList();
    CHECK(buffer_list.size() == 2); // NOLINT
    CHECK(buffer_list[0] == 0); // NOLINT
    CHECK(buffer_list[1] == 1); // NOLINT
  }
  const auto& barriers_prepass = render_graph.GetBarriersPrePass();
  const auto& barriers_postpass = render_graph.GetBarriersPostPass();
  CHECK(barriers_prepass.size() == 2); // NOLINT
  CHECK(barriers_prepass[0].size() == 1); // NOLINT
  CHECK(barriers_prepass[0][0].buffer_id == 1); // NOLINT
  CHECK(barriers_prepass[0][0].split_type == BarrierSplitType::kBegin); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[0][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_before == kBufferStateFlagPresent); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_prepass[1].size() == 2); // NOLINT
  CHECK(barriers_prepass[1][0].buffer_id == 0); // NOLINT
  CHECK(barriers_prepass[1][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_after  == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(barriers_prepass[1][1].buffer_id == 1); // NOLINT
  CHECK(barriers_prepass[1][1].split_type == BarrierSplitType::kEnd); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][1].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_before == kBufferStateFlagPresent); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_postpass.size() == 2); // NOLINT
  CHECK(barriers_postpass[0].empty()); // NOLINT
  CHECK(barriers_postpass[1].size() == 2); // NOLINT
  CHECK(barriers_postpass[1][0].buffer_id == 0); // NOLINT
  CHECK(barriers_postpass[1][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_before == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][0].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_postpass[1][1].buffer_id == 1); // NOLINT
  CHECK(barriers_postpass[1][1].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][1].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_after  == kBufferStateFlagPresent); // NOLINT
  {
    auto& render_pass_buffer_clear_info = render_graph.GetRenderPassBufferClearInfo();
    CHECK(render_pass_buffer_clear_info.size() == 2); // NOLINT
    CHECK(render_pass_buffer_clear_info[0].size() == 1); // NOLINT
    CHECK(render_pass_buffer_clear_info[0].contains(0)); // NOLINT
    CHECK(render_pass_buffer_clear_info[0].at(0) == BufferClearType::kDontCare); // NOLINT
    CHECK(render_pass_buffer_clear_info[1].empty()); // NOLINT
  }
}
TEST_CASE("barrier for use compute queue") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  render_graph_config.SetBufferSizeInfoFunction(GetBufferSizeAndAlignmentImpl);
  render_graph_config.AddMandatoryBufferName(StrId("swapchain"));
  auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("draw"), .command_queue_type = CommandQueueType::kCompute});
  CHECK(render_graph_config.GetRenderPassIndex(StrId("draw")) == 0); // NOLINT
  render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagUav, kWriteFlag, BufferClearFlag::kNone});
  render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("present"), .command_queue_type = CommandQueueType::kGraphics});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagUav, kReadFlag, BufferClearFlag::kNone});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("swapchain"), kBufferStateFlagRtv, kWriteFlag, BufferClearFlag::kNone});
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  CHECK(render_graph_config.GetRenderPassIndex(StrId("draw")) == 0); // NOLINT
  CHECK(render_graph_config.GetRenderPassIndex(StrId("present")) == 1); // NOLINT
  CHECK(render_graph_config.GetRenderPassIndex(StrId("present")) == render_pass_index); // NOLINT
  RenderGraph render_graph(&memory_resource_scene);
  render_graph.Build(render_graph_config, &memory_resource_work);
  const auto& barriers_prepass = render_graph.GetBarriersPrePass();
  const auto& barriers_postpass = render_graph.GetBarriersPostPass();
  CHECK(barriers_prepass.size() == 2); // NOLINT
  CHECK(barriers_prepass[0].empty()); // NOLINT
  CHECK(barriers_prepass[1].size() == 2); // NOLINT
  CHECK(barriers_prepass[1][0].buffer_id == 0); // NOLINT
  CHECK(barriers_prepass[1][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierUav>(barriers_prepass[1][0].params)); // NOLINT
  CHECK(barriers_prepass[1][1].buffer_id == 1); // NOLINT
  CHECK(barriers_prepass[1][1].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][1].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_before == kBufferStateFlagPresent); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][1].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_postpass.size() == 2); // NOLINT
  CHECK(barriers_postpass[0].empty()); // NOLINT
  CHECK(barriers_postpass[1].size() == 2); // NOLINT
  CHECK(barriers_postpass[1][0].buffer_id == 0); // NOLINT
  CHECK(barriers_postpass[1][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierUav>(barriers_postpass[1][0].params)); // NOLINT
  CHECK(barriers_postpass[1][1].buffer_id == 1); // NOLINT
  CHECK(barriers_postpass[1][1].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][1].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_after  == kBufferStateFlagPresent); // NOLINT
  const auto& queue_signals = render_graph.GetQueueSignals();
  CHECK(queue_signals.size() == 2); // NOLINT
  CHECK(queue_signals.contains(0)); // NOLINT
  CHECK(queue_signals.at(0).size() == 1); // NOLINT
  CHECK(queue_signals.at(0).contains(1)); // NOLINT
  CHECK(queue_signals.contains(1)); // NOLINT
  CHECK(queue_signals.at(1).size() == 1); // NOLINT
  CHECK(queue_signals.at(1).contains(0)); // NOLINT
}
TEST_CASE("buffer reuse") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("0"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag, BufferClearFlag::kNone});
  }
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("1"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag, BufferClearFlag::kNone});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag, BufferClearFlag::kClear});
  }
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("2"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag, BufferClearFlag::kNone});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag, BufferClearFlag::kNone});
  }
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("present"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag, BufferClearFlag::kNone});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("swapchain"), kBufferStateFlagRtv, kWriteFlag, BufferClearFlag::kNone});
  }
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.SetBufferSizeInfoFunction(GetBufferSizeAndAlignmentImpl);
  render_graph_config.AddExternalBufferName(StrId("swapchain"));
  render_graph_config.AddMandatoryBufferName(StrId("swapchain"));
  CHECK(render_graph_config.GetExternalBufferNameList().size() == 1); // NOLINT
  CHECK(render_graph_config.GetExternalBufferNameList().contains(StrId("swapchain"))); // NOLINT
  unordered_map<BufferId, unordered_map<BufferId, int32_t>> node_distance_map;
  {
    auto render_pass_num = render_graph_config.GetRenderPassNum();
    auto [buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, render_pass_buffer_clear_info] = InitBufferIdList(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_buffer_clear_info.size() == 4);
    CHECK(render_pass_buffer_clear_info[0].empty());
    CHECK(render_pass_buffer_clear_info[1].size() == 1);
    CHECK(render_pass_buffer_clear_info[1].at(1) == BufferClearType::kClear);
    CHECK(render_pass_buffer_clear_info[2].empty());
    CHECK(render_pass_buffer_clear_info[3].empty());
    auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num, buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, &memory_resource_work);
    auto buffer_name_id_map = GetBufferNameIdMap(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, &memory_resource_work);
    auto initial_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferInitialStateList(), &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    auto final_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferFinalStateList(), &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work); // NOLINT
    // std::tie(buffer_state_list, buffer_user_pass_list) = RevertBufferStateToInitialState(std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    auto render_pass_command_queue_type_list = vector<CommandQueueType>(render_graph_config.GetRenderPassCommandQueueTypeList(), &memory_resource_work);
    node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(render_pass_num, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
    auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work); // NOLINT
    inter_queue_pass_dependency = RemoveRedundantDependencyFromSameQueuePredecessors(render_pass_num, render_pass_command_queue_type_list, std::move(inter_queue_pass_dependency), &memory_resource_work);
    node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
    node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
    auto buffer_config_list = CreateBufferConfigList(buffer_id_list,
                                                     CreateBufferIdNameMap(render_pass_num, static_cast<uint32_t>(buffer_id_list.size()), render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, &memory_resource_work),
                                                     render_graph_config.GetPrimaryBufferWidth(), render_graph_config.GetPrimaryBufferHeight(),
                                                     render_graph_config.GetSwapchainBufferWidth(), render_graph_config.GetSwapchainBufferHeight(),
                                                     render_graph_config.GetBufferSizeInfoList(),
                                                     buffer_state_list, // NOLINT
                                                     render_graph_config.GetBufferDefaultClearValueList(),
                                                     render_graph_config.GetBufferDimensionTypeList(),
                                                     render_graph_config.GetBufferFormatList(),
                                                     render_graph_config.GetBufferDepthStencilFlagList(),
                                                     &memory_resource_work);
    unordered_set<BufferId> excluded_buffers{&memory_resource_work};
    excluded_buffers.insert(3);
    auto [render_pass_new_buffer_list, render_pass_expired_buffer_list] = ConfigureRenderPassNewAndExpiredBuffers(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), buffer_id_list, buffer_user_pass_list, &memory_resource_work);
    unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
    buffer_size_list.insert_or_assign(0, 8); // NOLINT
    buffer_size_list.insert_or_assign(1, 8); // NOLINT
    buffer_size_list.insert_or_assign(2, 8); // NOLINT
    buffer_size_list.insert_or_assign(3, 8); // NOLINT
    buffer_size_list.insert_or_assign(4, 8); // NOLINT
    unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
    buffer_alignment_list.insert_or_assign(0, 8); // NOLINT
    buffer_alignment_list.insert_or_assign(1, 8); // NOLINT
    buffer_alignment_list.insert_or_assign(2, 8); // NOLINT
    buffer_alignment_list.insert_or_assign(3, 8); // NOLINT
    buffer_alignment_list.insert_or_assign(4, 8); // NOLINT
    auto concurrent_pass_list = FindConcurrentPass(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), node_distance_map, &memory_resource_work);
    CHECK(concurrent_pass_list.size() == 4); // NOLINT
    CHECK(concurrent_pass_list[0].empty()); // NOLINT
    CHECK(concurrent_pass_list[1].empty()); // NOLINT
    CHECK(concurrent_pass_list[2].empty()); // NOLINT
    CHECK(concurrent_pass_list[3].empty()); // NOLINT
    auto concurrent_buffer_list = FindConcurrentBuffers(concurrent_pass_list, render_pass_buffer_id_list, &memory_resource_work);
    auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, excluded_buffers, true, &memory_resource_work, &memory_resource_work);
    CHECK(renamed_buffers.size() == 1); // NOLINT
    CHECK(renamed_buffers.contains(2)); // NOLINT
    CHECK(renamed_buffers.at(2) == 0); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list.empty()); // NOLINT
    CHECK(render_pass_after_memory_aliasing_list.empty()); // NOLINT
    buffer_id_list = UpdateBufferIdListUsingReusableBuffers(renamed_buffers, std::move(buffer_id_list));
    CHECK(buffer_id_list.size() == 3); // NOLINT
    CHECK(buffer_id_list[0] == 0); // NOLINT
    CHECK(buffer_id_list[1] == 1); // NOLINT
    CHECK(buffer_id_list[2] == 3); // NOLINT
    render_pass_buffer_id_list = UpdateRenderPassBufferIdListUsingReusableBuffers(renamed_buffers, std::move(render_pass_buffer_id_list));
    CHECK(render_pass_buffer_id_list[0][0] == 0); // NOLINT
    CHECK(render_pass_buffer_id_list[1][0] == 0); // NOLINT
    CHECK(render_pass_buffer_id_list[1][1] == 1); // NOLINT
    CHECK(render_pass_buffer_id_list[2][0] == 1); // NOLINT
    CHECK(render_pass_buffer_id_list[2][1] == 0); // NOLINT
    CHECK(render_pass_buffer_id_list[3][0] == 0); // NOLINT
    CHECK(render_pass_buffer_id_list[3][1] == 3); // NOLINT
    std::tie(buffer_state_list, buffer_user_pass_list) = UpdateBufferStateListAndUserPassListUsingReusableBuffers(renamed_buffers, std::move(buffer_state_list), std::move(buffer_user_pass_list));
    CHECK(buffer_state_list.size() == 3); // NOLINT
    CHECK(!buffer_state_list.contains(2)); // NOLINT
    CHECK(buffer_state_list.at(0).size() == 4); // NOLINT
    CHECK(buffer_state_list.at(0)[0] == kBufferStateFlagRtv); // NOLINT
    CHECK(buffer_state_list.at(0)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
    CHECK(buffer_state_list.at(0)[2] == kBufferStateFlagRtv); // NOLINT
    CHECK(buffer_state_list.at(0)[3] == kBufferStateFlagSrvPsOnly); // NOLINT
    CHECK(buffer_user_pass_list.size() == 3); // NOLINT
    CHECK(!buffer_user_pass_list.contains(2)); // NOLINT
    CHECK(buffer_user_pass_list.at(0).size() == 4); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[0].size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[0][0] == 0); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[1].size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[1][0] == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[2].size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[2][0] == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[3].size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[3][0] == 3); // NOLINT
    unordered_map<BufferId, BufferStateFlags> flag_copy{&memory_resource_work};
    for (const auto& [id, config] : buffer_config_list) {
      flag_copy.insert_or_assign(id, config.state_flags);
    }
    buffer_config_list = MergeReusedBufferConfigs(renamed_buffers, std::move(buffer_config_list));
    CHECK(buffer_config_list.size() == 3); // NOLINT
    CHECK(!buffer_config_list.contains(2)); // NOLINT
    for (const auto& [id, config] : buffer_config_list) {
      CHECK(config.state_flags == flag_copy.at(id)); // NOLINT
    }
  }
  RenderGraph render_graph(&memory_resource_scene);
  render_graph.Build(render_graph_config, &memory_resource_work);
  CHECK(render_graph.GetBufferIdList().size() == 3); // NOLINT
  const auto& render_pass_buffer_id_list = render_graph.GetRenderPassBufferIdList();
  CHECK(render_pass_buffer_id_list[0][0] == render_pass_buffer_id_list[1][0]); // NOLINT
  CHECK(render_pass_buffer_id_list[1][1] == render_pass_buffer_id_list[2][0]); // NOLINT
  CHECK(render_pass_buffer_id_list[2][1] == render_pass_buffer_id_list[0][0]); // NOLINT
  CHECK(render_pass_buffer_id_list[2][1] == render_pass_buffer_id_list[3][0]); // NOLINT
  CHECK(render_pass_buffer_id_list[3][1] != render_pass_buffer_id_list[0][0]); // NOLINT
  CHECK(render_pass_buffer_id_list[3][1] != render_pass_buffer_id_list[1][1]); // NOLINT
  CHECK(render_graph.GetQueueSignals().empty()); // NOLINT
  const auto& barriers_prepass = render_graph.GetBarriersPrePass();
  CHECK(barriers_prepass.size() == 4); // NOLINT
  CHECK(barriers_prepass[0].size() == 1); // NOLINT
  CHECK(barriers_prepass[0][0].buffer_id == 3); // NOLINT
  CHECK(barriers_prepass[0][0].split_type == BarrierSplitType::kBegin); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[0][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_before == kBufferStateFlagPresent); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_prepass[1].size() == 1); // NOLINT
  CHECK(barriers_prepass[1][0].buffer_id == 0); // NOLINT
  CHECK(barriers_prepass[1][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_after  == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(barriers_prepass[2].size() == 2); // NOLINT
  CHECK(barriers_prepass[2][0].buffer_id == 0); // NOLINT
  CHECK(barriers_prepass[2][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[2][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][0].params).state_before == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][0].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_prepass[2][1].buffer_id == 1); // NOLINT
  CHECK(barriers_prepass[2][1].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[2][1].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][1].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][1].params).state_after  == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(barriers_prepass[3].size() == 2); // NOLINT
  CHECK(barriers_prepass[3][0].buffer_id == 0); // NOLINT
  CHECK(barriers_prepass[3][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[3][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][0].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][0].params).state_after  == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(barriers_prepass[3][1].buffer_id == 3); // NOLINT
  CHECK(barriers_prepass[3][1].split_type == BarrierSplitType::kEnd); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[3][1].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][1].params).state_before == kBufferStateFlagPresent); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][1].params).state_after  == kBufferStateFlagRtv); // NOLINT
  const auto& barriers_postpass = render_graph.GetBarriersPostPass();
  CHECK(barriers_postpass.size() == 4); // NOLINT
  CHECK(barriers_postpass[0].empty()); // NOLINT
  CHECK(barriers_postpass[1].empty()); // NOLINT
  CHECK(barriers_postpass[2].size() == 1); // NOLINT
  CHECK(barriers_postpass[2][0].buffer_id == 1); // NOLINT
  CHECK(barriers_postpass[2][0].split_type == BarrierSplitType::kBegin); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[2][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[2][0].params).state_before == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[2][0].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_postpass[3].size() == 3); // NOLINT
  CHECK(barriers_postpass[3][0].buffer_id == 0); // NOLINT
  CHECK(barriers_postpass[3][0].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[3][0].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][0].params).state_before == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][0].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_postpass[3][1].buffer_id == 1); // NOLINT
  CHECK(barriers_postpass[3][1].split_type == BarrierSplitType::kEnd); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[3][1].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][1].params).state_before == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][1].params).state_after  == kBufferStateFlagRtv); // NOLINT
  CHECK(barriers_postpass[3][2].buffer_id == 3); // NOLINT
  CHECK(barriers_postpass[3][2].split_type == BarrierSplitType::kNone); // NOLINT
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[3][2].params)); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][2].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][2].params).state_after  == kBufferStateFlagPresent); // NOLINT
}
TEST_CASE("memory aliasing") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<vector<BufferId>> render_pass_buffer_id_list{&memory_resource_work};
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(0);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(0);
  render_pass_buffer_id_list.back().push_back(1);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(0);
  render_pass_buffer_id_list.back().push_back(2);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(1);
  render_pass_buffer_id_list.back().push_back(2);
  render_pass_buffer_id_list.back().push_back(3);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(3);
  render_pass_buffer_id_list.back().push_back(4);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  unordered_map<uint32_t, unordered_set<uint32_t>> inter_queue_pass_dependency_from_descendant_to_ancestors{&memory_resource_work};
  vector<BufferId> buffer_id_list{&memory_resource_work};
  buffer_id_list.push_back(0);
  buffer_id_list.push_back(1);
  buffer_id_list.push_back(2);
  buffer_id_list.push_back(3);
  buffer_id_list.push_back(4);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(1);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(2);
  buffer_user_pass_list.insert_or_assign(1, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(1);
  buffer_user_pass_list.at(1).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(1).back().push_back(3);
  buffer_user_pass_list.insert_or_assign(2, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(2).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(2).back().push_back(2);
  buffer_user_pass_list.at(2).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(2).back().push_back(3);
  buffer_user_pass_list.insert_or_assign(3, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(3).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(3).back().push_back(3);
  buffer_user_pass_list.at(3).back().push_back(4);
  buffer_user_pass_list.insert_or_assign(4, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(4).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(4).back().push_back(4);
  auto [render_pass_new_buffer_list, render_pass_expired_buffer_list] = ConfigureRenderPassNewAndExpiredBuffers(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), buffer_id_list, buffer_user_pass_list, &memory_resource_work);
  CHECK(render_pass_new_buffer_list.size() == 5); // NOLINT
  CHECK(render_pass_new_buffer_list[0].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[0][0] == 0); // NOLINT
  CHECK(render_pass_new_buffer_list[1].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[1][0] == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[2].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[2][0] == 2); // NOLINT
  CHECK(render_pass_new_buffer_list[3].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[3][0] == 3); // NOLINT
  CHECK(render_pass_new_buffer_list[4].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[4][0] == 4); // NOLINT
  CHECK(render_pass_expired_buffer_list.size() == 5); // NOLINT
  CHECK(render_pass_expired_buffer_list[0].empty()); // NOLINT
  CHECK(render_pass_expired_buffer_list[1].empty()); // NOLINT
  CHECK(render_pass_expired_buffer_list[2].size() == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[2][0] == 0); // NOLINT
  CHECK(render_pass_expired_buffer_list[3].size() == 2); // NOLINT
  CHECK(render_pass_expired_buffer_list[3][0] == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[3][1] == 2); // NOLINT
  CHECK(render_pass_expired_buffer_list[4].size() == 2); // NOLINT
  CHECK(render_pass_expired_buffer_list[4][0] == 3); // NOLINT
  CHECK(render_pass_expired_buffer_list[4][1] == 4); // NOLINT
  unordered_map<BufferId, BufferConfig> buffer_config_list{&memory_resource_work};
  buffer_config_list.insert_or_assign(0, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(1, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(2, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(3, BufferConfig{.width=1,.height=2,.state_flags=MergeBufferStateFlags(kBufferStateFlagRtv,kBufferStateFlagSrvPsOnly),.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(4, BufferConfig{.width=3,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
  buffer_size_list.insert_or_assign(0, 8); // NOLINT
  buffer_size_list.insert_or_assign(1, 8); // NOLINT
  buffer_size_list.insert_or_assign(2, 8); // NOLINT
  buffer_size_list.insert_or_assign(3, 8); // NOLINT
  buffer_size_list.insert_or_assign(4, 16); // NOLINT
  unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
  buffer_alignment_list.insert_or_assign(0, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(1, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(2, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(3, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(4, 8); // NOLINT
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  auto concurrent_pass_list = FindConcurrentPass(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), node_distance_map, &memory_resource_work);
  CHECK(concurrent_pass_list.size() == 5); // NOLINT
  CHECK(concurrent_pass_list[0].empty()); // NOLINT
  CHECK(concurrent_pass_list[1].empty()); // NOLINT
  CHECK(concurrent_pass_list[2].empty()); // NOLINT
  CHECK(concurrent_pass_list[3].empty()); // NOLINT
  CHECK(concurrent_pass_list[4].empty()); // NOLINT
  auto concurrent_buffer_list = FindConcurrentBuffers(concurrent_pass_list, render_pass_buffer_id_list, &memory_resource_work);
  auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, true, &memory_resource_work, &memory_resource_work);
  CHECK(buffer_address_offset_list.size() == 5); // NOLINT
  CHECK(buffer_address_offset_list.at(0) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(1) == 8); // NOLINT
  CHECK(buffer_address_offset_list.at(2) == 16); // NOLINT
  CHECK(buffer_address_offset_list.at(3) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(4) == 8); // NOLINT
  CHECK(renamed_buffers.size() == 1); // NOLINT
  CHECK(renamed_buffers.at(3) == 0); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.size() == 1); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(1)); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(2)); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(3)); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.contains(4)); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(4)[0] == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.size() == 1); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(1)); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(2)); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(3)); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.contains(4)); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(4)[0] == 4); // NOLINT
}
TEST_CASE("ConfigureBufferValidPassList w/async compute") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<vector<BufferId>> render_pass_buffer_id_list{&memory_resource_work};
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(0); // NOLINT
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(1); // NOLINT
  render_pass_buffer_id_list.back().push_back(7); // NOLINT
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(1); // NOLINT
  render_pass_buffer_id_list.back().push_back(2); // NOLINT
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(3); // NOLINT
  render_pass_buffer_id_list.back().push_back(8); // NOLINT
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(3); // NOLINT
  render_pass_buffer_id_list.back().push_back(4); // NOLINT
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(4); // NOLINT
  render_pass_buffer_id_list.back().push_back(5); // NOLINT
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(5); // NOLINT
  render_pass_buffer_id_list.back().push_back(6); // NOLINT
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  auto [buffer_id_list, buffer_user_pass_list] = CollectBufferDataForTest(render_pass_buffer_id_list, &memory_resource_work);
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  inter_queue_pass_dependency = RemoveRedundantDependencyFromSameQueuePredecessors(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, std::move(inter_queue_pass_dependency), &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 3); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(0)); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(1)); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(2)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(2).contains(1)); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(3)); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(4)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(4).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(4).contains(3)); // NOLINT
  CHECK(!inter_queue_pass_dependency.contains(5)); // NOLINT
  CHECK(inter_queue_pass_dependency.contains(6)); // NOLINT
  CHECK(inter_queue_pass_dependency.at(6).size() == 1); // NOLINT
  CHECK(inter_queue_pass_dependency.at(6).contains(5)); // NOLINT
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = ConfigureTripleInterQueueDependency(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.at(0).size() == 3); // NOLINT
  CHECK(node_distance_map.at(0).contains(0)); // NOLINT
  CHECK(node_distance_map.at(0).at(0) == 0); // NOLINT
  CHECK(!node_distance_map.at(0).contains(1)); // NOLINT
  CHECK(node_distance_map.at(0).contains(2)); // NOLINT
  CHECK(node_distance_map.at(0).at(2) == 1); // NOLINT
  CHECK(!node_distance_map.at(0).contains(3)); // NOLINT
  CHECK(!node_distance_map.at(0).contains(4)); // NOLINT
  CHECK(!node_distance_map.at(0).contains(5)); // NOLINT
  CHECK(node_distance_map.at(0).contains(6)); // NOLINT
  CHECK(node_distance_map.at(0).at(6) == 2); // NOLINT
  CHECK(node_distance_map.at(1).size() == 6); // NOLINT
  CHECK(!node_distance_map.at(1).contains(0)); // NOLINT
  CHECK(node_distance_map.at(1).contains(1)); // NOLINT
  CHECK(node_distance_map.at(1).at(1) == 0); // NOLINT
  CHECK(node_distance_map.at(1).contains(2)); // NOLINT
  CHECK(node_distance_map.at(1).at(2) == 1); // NOLINT
  CHECK(node_distance_map.at(1).contains(3)); // NOLINT
  CHECK(node_distance_map.at(1).at(3) == 1); // NOLINT
  CHECK(node_distance_map.at(1).contains(4)); // NOLINT
  CHECK(node_distance_map.at(1).at(4) == 2); // NOLINT
  CHECK(node_distance_map.at(1).contains(5)); // NOLINT
  CHECK(node_distance_map.at(1).at(5) == 3); // NOLINT
  CHECK(node_distance_map.at(1).contains(6)); // NOLINT
  CHECK(node_distance_map.at(1).at(6) == 2); // NOLINT
  CHECK(node_distance_map.at(2).size() == 4); // NOLINT
  CHECK(node_distance_map.at(2).contains(0)); // NOLINT
  CHECK(node_distance_map.at(2).at(0) == -1); // NOLINT
  CHECK(node_distance_map.at(2).contains(1)); // NOLINT
  CHECK(node_distance_map.at(2).at(1) == -1); // NOLINT
  CHECK(node_distance_map.at(2).contains(2)); // NOLINT
  CHECK(node_distance_map.at(2).at(2) == 0); // NOLINT
  CHECK(!node_distance_map.at(2).contains(3)); // NOLINT
  CHECK(!node_distance_map.at(2).contains(4)); // NOLINT
  CHECK(!node_distance_map.at(2).contains(5)); // NOLINT
  CHECK(node_distance_map.at(2).contains(6)); // NOLINT
  CHECK(node_distance_map.at(2).at(6) == 1); // NOLINT
  CHECK(node_distance_map.at(3).size() == 5); // NOLINT
  CHECK(!node_distance_map.at(3).contains(0)); // NOLINT
  CHECK(node_distance_map.at(3).contains(1)); // NOLINT
  CHECK(node_distance_map.at(3).at(1) == -1); // NOLINT
  CHECK(!node_distance_map.at(3).contains(2)); // NOLINT
  CHECK(node_distance_map.at(3).contains(3)); // NOLINT
  CHECK(node_distance_map.at(3).at(3) == 0); // NOLINT
  CHECK(node_distance_map.at(3).contains(4)); // NOLINT
  CHECK(node_distance_map.at(3).at(4) == 1); // NOLINT
  CHECK(node_distance_map.at(3).contains(5)); // NOLINT
  CHECK(node_distance_map.at(3).at(5) == 2); // NOLINT
  CHECK(node_distance_map.at(3).contains(6)); // NOLINT
  CHECK(node_distance_map.at(3).at(6) == 3); // NOLINT
  CHECK(node_distance_map.at(4).size() == 5); // NOLINT
  CHECK(!node_distance_map.at(4).contains(0)); // NOLINT
  CHECK(node_distance_map.at(4).contains(1)); // NOLINT
  CHECK(node_distance_map.at(4).at(1) == -2); // NOLINT
  CHECK(!node_distance_map.at(4).contains(2)); // NOLINT
  CHECK(node_distance_map.at(4).contains(3)); // NOLINT
  CHECK(node_distance_map.at(4).at(3) == -1); // NOLINT
  CHECK(node_distance_map.at(4).contains(4)); // NOLINT
  CHECK(node_distance_map.at(4).at(4) == 0); // NOLINT
  CHECK(node_distance_map.at(4).contains(5)); // NOLINT
  CHECK(node_distance_map.at(4).at(5) == 1); // NOLINT
  CHECK(node_distance_map.at(4).contains(6)); // NOLINT
  CHECK(node_distance_map.at(4).at(6) == 2); // NOLINT
  CHECK(node_distance_map.at(5).size() == 5); // NOLINT
  CHECK(!node_distance_map.at(5).contains(0)); // NOLINT
  CHECK(node_distance_map.at(5).contains(1)); // NOLINT
  CHECK(node_distance_map.at(5).at(1) == -3); // NOLINT
  CHECK(!node_distance_map.at(5).contains(2)); // NOLINT
  CHECK(node_distance_map.at(5).contains(3)); // NOLINT
  CHECK(node_distance_map.at(5).at(3) == -2); // NOLINT
  CHECK(node_distance_map.at(5).contains(4)); // NOLINT
  CHECK(node_distance_map.at(5).at(4) == -1); // NOLINT
  CHECK(node_distance_map.at(5).contains(5)); // NOLINT
  CHECK(node_distance_map.at(5).at(5) == 0); // NOLINT
  CHECK(node_distance_map.at(5).contains(6)); // NOLINT
  CHECK(node_distance_map.at(5).at(6) == 1); // NOLINT
  CHECK(node_distance_map.at(6).size() == 7); // NOLINT
  CHECK(node_distance_map.at(6).contains(0)); // NOLINT
  CHECK(node_distance_map.at(6).contains(1)); // NOLINT
  CHECK(node_distance_map.at(6).contains(2)); // NOLINT
  CHECK(node_distance_map.at(6).contains(3)); // NOLINT
  CHECK(node_distance_map.at(6).contains(4)); // NOLINT
  CHECK(node_distance_map.at(6).contains(5)); // NOLINT
  CHECK(node_distance_map.at(6).contains(6)); // NOLINT
  CHECK(node_distance_map.at(6).contains(0)); // NOLINT
  CHECK(node_distance_map.at(6).at(0) == -2); // NOLINT
  CHECK(node_distance_map.at(6).at(1) == -2); // NOLINT
  CHECK(node_distance_map.at(6).at(2) == -1); // NOLINT
  CHECK(node_distance_map.at(6).at(3) == -3); // NOLINT
  CHECK(node_distance_map.at(6).at(4) == -2); // NOLINT
  CHECK(node_distance_map.at(6).at(5) == -1); // NOLINT
  CHECK(node_distance_map.at(6).at(6) == 0); // NOLINT
  auto [render_pass_new_buffer_list, render_pass_expired_buffer_list] = ConfigureRenderPassNewAndExpiredBuffers(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), buffer_id_list, buffer_user_pass_list, &memory_resource_work);
  CHECK(render_pass_new_buffer_list.size() == 7); // NOLINT
  CHECK(render_pass_new_buffer_list[0].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[0][0] == 0); // NOLINT
  CHECK(render_pass_new_buffer_list[1].size() == 2); // NOLINT
  CHECK(render_pass_new_buffer_list[1][0] == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[1][1] == 7); // NOLINT
  CHECK(render_pass_new_buffer_list[2].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[2][0] == 2); // NOLINT
  CHECK(render_pass_new_buffer_list[3].size() == 2); // NOLINT
  CHECK(render_pass_new_buffer_list[3][0] == 3); // NOLINT
  CHECK(render_pass_new_buffer_list[3][1] == 8); // NOLINT
  CHECK(render_pass_new_buffer_list[4].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[4][0] == 4); // NOLINT
  CHECK(render_pass_new_buffer_list[5].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[5][0] == 5); // NOLINT
  CHECK(render_pass_new_buffer_list[6].size() == 1); // NOLINT
  CHECK(render_pass_new_buffer_list[6][0] == 6); // NOLINT
  CHECK(render_pass_expired_buffer_list.size() == 7); // NOLINT
  CHECK(render_pass_expired_buffer_list[0].size() == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[0][0] == 0); // NOLINT
  CHECK(render_pass_expired_buffer_list[1].size() == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[1][0] == 7); // NOLINT
  CHECK(render_pass_expired_buffer_list[2].size() == 2); // NOLINT
  CHECK(render_pass_expired_buffer_list[2][0] == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[2][1] == 2); // NOLINT
  CHECK(render_pass_expired_buffer_list[3].size() == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[3][0] == 8); // NOLINT
  CHECK(render_pass_expired_buffer_list[4].size() == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[4][0] == 3); // NOLINT
  CHECK(render_pass_expired_buffer_list[5].size() == 1); // NOLINT
  CHECK(render_pass_expired_buffer_list[5][0] == 4); // NOLINT
  CHECK(render_pass_expired_buffer_list[6].size() == 2); // NOLINT
  CHECK(render_pass_expired_buffer_list[6][0] == 5); // NOLINT
  CHECK(render_pass_expired_buffer_list[6][1] == 6); // NOLINT
  unordered_map<BufferId, BufferConfig> buffer_config_list{&memory_resource_work};
  buffer_config_list.insert_or_assign(0, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(1, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(2, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(3, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(4, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(5, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(6, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(7, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(8, buffer_config_list.at(0)); // NOLINT
  unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
  buffer_size_list.insert_or_assign(0, 4); // NOLINT
  buffer_size_list.insert_or_assign(1, 4); // NOLINT
  buffer_size_list.insert_or_assign(2, 4); // NOLINT
  buffer_size_list.insert_or_assign(3, 4); // NOLINT
  buffer_size_list.insert_or_assign(4, 4); // NOLINT
  buffer_size_list.insert_or_assign(5, 4); // NOLINT
  buffer_size_list.insert_or_assign(6, 4); // NOLINT
  buffer_size_list.insert_or_assign(7, 4); // NOLINT
  buffer_size_list.insert_or_assign(8, 4); // NOLINT
  unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
  buffer_alignment_list.insert_or_assign(0, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(1, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(2, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(3, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(4, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(5, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(6, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(7, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(8, 8); // NOLINT
  auto concurrent_pass_list = FindConcurrentPass(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), node_distance_map, &memory_resource_work);
  CHECK(concurrent_pass_list.size() == 7); // NOLINT
  CHECK(concurrent_pass_list[0].size() == 4); // NOLINT
  CHECK(concurrent_pass_list[0][0] == 1); // NOLINT
  CHECK(concurrent_pass_list[0][1] == 3); // NOLINT
  CHECK(concurrent_pass_list[0][2] == 4); // NOLINT
  CHECK(concurrent_pass_list[0][3] == 5); // NOLINT
  CHECK(concurrent_pass_list[1].size() == 1); // NOLINT
  CHECK(concurrent_pass_list[1][0] == 0); // NOLINT
  CHECK(concurrent_pass_list[2].size() == 3); // NOLINT
  CHECK(concurrent_pass_list[2][0] == 3); // NOLINT
  CHECK(concurrent_pass_list[2][1] == 4); // NOLINT
  CHECK(concurrent_pass_list[2][2] == 5); // NOLINT
  CHECK(concurrent_pass_list[3].size() == 2); // NOLINT
  CHECK(concurrent_pass_list[3][0] == 0); // NOLINT
  CHECK(concurrent_pass_list[3][1] == 2); // NOLINT
  CHECK(concurrent_pass_list[4].size() == 2); // NOLINT
  CHECK(concurrent_pass_list[4][0] == 0); // NOLINT
  CHECK(concurrent_pass_list[4][1] == 2); // NOLINT
  CHECK(concurrent_pass_list[5].size() == 2); // NOLINT
  CHECK(concurrent_pass_list[5][0] == 0); // NOLINT
  CHECK(concurrent_pass_list[5][1] == 2); // NOLINT
  CHECK(concurrent_pass_list[6].empty()); // NOLINT
  auto concurrent_buffer_list = FindConcurrentBuffers(concurrent_pass_list, render_pass_buffer_id_list, &memory_resource_work);
  CHECK(concurrent_buffer_list.size() == 7); // NOLINT
  SUBCASE("reuse only") {
    auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, true, &memory_resource_work, &memory_resource_work);
    CHECK(buffer_address_offset_list.size() == 9); // NOLINT
    CHECK(buffer_address_offset_list.at(0) == 0); // NOLINT
    CHECK(buffer_address_offset_list.at(1) == 8); // NOLINT
    CHECK(buffer_address_offset_list.at(2) == 0); // NOLINT
    CHECK(buffer_address_offset_list.at(3) == 16); // NOLINT
    CHECK(buffer_address_offset_list.at(4) == 24); // NOLINT
    CHECK(buffer_address_offset_list.at(5) == 16); // NOLINT
    CHECK(buffer_address_offset_list.at(6) == 0); // NOLINT
    CHECK(buffer_address_offset_list.at(7) == 16); // NOLINT
    CHECK(buffer_address_offset_list.at(8) == 24); // NOLINT
    CHECK(renamed_buffers.size() == 5); // NOLINT
    CHECK(renamed_buffers.at(2) == 0); // NOLINT
    CHECK(renamed_buffers.at(6) == 2); // NOLINT
    CHECK(renamed_buffers.at(5) == 3); // NOLINT
    CHECK(renamed_buffers.at(3) == 7); // NOLINT
    CHECK(renamed_buffers.at(4) == 8); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list.empty()); // NOLINT
    CHECK(render_pass_after_memory_aliasing_list.empty()); // NOLINT
    renamed_buffers = MergeRenamedBufferDuplicativeBufferIds(buffer_id_list, std::move(renamed_buffers));
    CHECK(renamed_buffers.size() == 5); // NOLINT
    CHECK(renamed_buffers.at(2) == 0); // NOLINT
    CHECK(renamed_buffers.at(6) == 0); // NOLINT
    CHECK(renamed_buffers.at(5) == 7); // NOLINT
    CHECK(renamed_buffers.at(3) == 7); // NOLINT
    CHECK(renamed_buffers.at(4) == 8); // NOLINT
    buffer_id_list = UpdateBufferIdListUsingReusableBuffers(renamed_buffers, std::move(buffer_id_list));
    CHECK(buffer_id_list.size() == 4); // NOLINT
    CHECK(buffer_id_list[0] == 0); // NOLINT
    CHECK(buffer_id_list[1] == 1); // NOLINT
    CHECK(buffer_id_list[2] == 7); // NOLINT
    CHECK(buffer_id_list[3] == 8); // NOLINT
    render_pass_buffer_id_list = UpdateRenderPassBufferIdListUsingReusableBuffers(renamed_buffers, std::move(render_pass_buffer_id_list));
    CHECK(render_pass_buffer_id_list[0][0] == 0); // NOLINT
    CHECK(render_pass_buffer_id_list[1][0] == 1); // NOLINT
    CHECK(render_pass_buffer_id_list[1][1] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[2][0] == 1); // NOLINT
    CHECK(render_pass_buffer_id_list[2][1] == 0); // NOLINT
    CHECK(render_pass_buffer_id_list[3][0] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[3][1] == 8); // NOLINT
    CHECK(render_pass_buffer_id_list[4][0] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[4][1] == 8); // NOLINT
    CHECK(render_pass_buffer_id_list[5][0] == 8); // NOLINT
    CHECK(render_pass_buffer_id_list[5][1] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[6][0] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[6][1] == 0); // NOLINT
    auto buffer_state_list = CreateBufferStateListBasedOnBufferUserListForTest(buffer_user_pass_list, &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = UpdateBufferStateListAndUserPassListUsingReusableBuffers(renamed_buffers, std::move(buffer_state_list), std::move(buffer_user_pass_list));
    CHECK(buffer_state_list.size() == 4); // NOLINT
    CHECK(!buffer_state_list.contains(2)); // NOLINT
    CHECK(!buffer_state_list.contains(3)); // NOLINT
    CHECK(!buffer_state_list.contains(4)); // NOLINT
    CHECK(!buffer_state_list.contains(5)); // NOLINT
    CHECK(!buffer_state_list.contains(6)); // NOLINT
    CHECK(buffer_state_list.at(0).size() == 3); // NOLINT
    CHECK(buffer_state_list.at(0)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(0)[1] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(0)[2] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(1).size() == 2); // NOLINT
    CHECK(buffer_state_list.at(1)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(1)[1] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_state_list.at(7).size() == 5); // NOLINT
    CHECK(buffer_state_list.at(7)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(7)[1] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(7)[2] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_state_list.at(7)[3] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(7)[4] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_state_list.at(8).size() == 3); // NOLINT
    CHECK(buffer_state_list.at(8)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(8)[1] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(8)[2] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_user_pass_list.size() == 4); // NOLINT
    CHECK(!buffer_user_pass_list.contains(2)); // NOLINT
    CHECK(!buffer_user_pass_list.contains(3)); // NOLINT
    CHECK(!buffer_user_pass_list.contains(4)); // NOLINT
    CHECK(!buffer_user_pass_list.contains(5)); // NOLINT
    CHECK(!buffer_user_pass_list.contains(6)); // NOLINT
    CHECK(buffer_user_pass_list.at(0).size() == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[0][0] == 0); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[1][0] == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[2][0] == 6); // NOLINT
    CHECK(buffer_user_pass_list.at(1).size() == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[0][0] == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[1][0] == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(7).size() == 5); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[0][0] == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[1][0] == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[2][0] == 4); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[3][0] == 5); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[4][0] == 6); // NOLINT
    CHECK(buffer_user_pass_list.at(8).size() == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(8)[0][0] == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(8)[1][0] == 4); // NOLINT
    CHECK(buffer_user_pass_list.at(8)[2][0] == 5); // NOLINT
    for (const auto& [buffer_id, list_list] : buffer_user_pass_list) {
      auto buffer_id_copy = buffer_id;
      CAPTURE(buffer_id_copy);
      for (uint32_t i = 0; i < list_list.size(); i++) {
        CAPTURE(i);
        CHECK(list_list[i].size() == 1); // NOLINT
      }
    }
    unordered_map<BufferId, BufferStateFlags> flag_copy{&memory_resource_work};
    for (const auto& [id, config] : buffer_config_list) {
      flag_copy.insert_or_assign(id, config.state_flags);
    }
    buffer_config_list = MergeReusedBufferConfigs(renamed_buffers, std::move(buffer_config_list));
    CHECK(buffer_config_list.size() == 4); // NOLINT
    CHECK(buffer_config_list.contains(0)); // NOLINT
    CHECK(buffer_config_list.contains(1)); // NOLINT
    CHECK(!buffer_config_list.contains(2)); // NOLINT
    CHECK(!buffer_config_list.contains(3)); // NOLINT
    CHECK(!buffer_config_list.contains(4)); // NOLINT
    CHECK(!buffer_config_list.contains(5)); // NOLINT
    CHECK(!buffer_config_list.contains(6)); // NOLINT
    CHECK(buffer_config_list.contains(7)); // NOLINT
    CHECK(buffer_config_list.contains(8)); // NOLINT
    for (const auto& [id, config] : buffer_config_list) {
      CHECK(config.state_flags == flag_copy.at(id)); // NOLINT
    }
  }
  SUBCASE("with memory aliasing") {
    buffer_size_list.at(2) = 8; // NOLINT
    buffer_config_list.at(2).width = 123; // NOLINT
    buffer_config_list.at(3).state_flags = kBufferStateFlagDsv;
    auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, true, &memory_resource_work, &memory_resource_work);
    CHECK(buffer_address_offset_list.size() == 9); // NOLINT
    CHECK(buffer_address_offset_list.at(0) == 0); // NOLINT
    CHECK(buffer_address_offset_list.at(1) == 8); // NOLINT
    CHECK(buffer_address_offset_list.at(2) == 0); // NOLINT
    CHECK(buffer_address_offset_list.at(3) == 24); // NOLINT
    CHECK(buffer_address_offset_list.at(4) == 16); // NOLINT
    CHECK(buffer_address_offset_list.at(5) == 24); // NOLINT
    CHECK(buffer_address_offset_list.at(6) == 8); // NOLINT
    CHECK(buffer_address_offset_list.at(7) == 16); // NOLINT
    CHECK(buffer_address_offset_list.at(8) == 16); // NOLINT
    CHECK(renamed_buffers.size() == 3); // NOLINT
    CHECK(renamed_buffers.at(6) == 1); // NOLINT
    CHECK(renamed_buffers.at(8) == 7); // NOLINT
    CHECK(renamed_buffers.at(4) == 8); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list.size() == 2); // NOLINT
    CHECK(render_pass_after_memory_aliasing_list.size() == 2); // NOLINT
    CHECK(!render_pass_before_memory_aliasing_list.contains(0)); // NOLINT
    CHECK(!render_pass_before_memory_aliasing_list.contains(1)); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list.contains(2)); // NOLINT
    CHECK(!render_pass_before_memory_aliasing_list.contains(3)); // NOLINT
    CHECK(!render_pass_before_memory_aliasing_list.contains(4)); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list.contains(5)); // NOLINT
    CHECK(!render_pass_before_memory_aliasing_list.contains(6)); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list[2].size() == 1); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list[2][0] == 0); // NOLINT
    CHECK(render_pass_after_memory_aliasing_list[2].size() == 1); // NOLINT
    CHECK(render_pass_after_memory_aliasing_list[2][0] == 2); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list[5].size() == 1); // NOLINT
    CHECK(render_pass_before_memory_aliasing_list[5][0] == 3); // NOLINT
    CHECK(render_pass_after_memory_aliasing_list[5].size() == 1); // NOLINT
    CHECK(render_pass_after_memory_aliasing_list[5][0] == 5); // NOLINT
    renamed_buffers = MergeRenamedBufferDuplicativeBufferIds(buffer_id_list, std::move(renamed_buffers));
    CHECK(renamed_buffers.size() == 3); // NOLINT
    CHECK(renamed_buffers.at(6) == 1); // NOLINT
    CHECK(renamed_buffers.at(8) == 7); // NOLINT
    CHECK(renamed_buffers.at(4) == 7); // NOLINT
    buffer_id_list = UpdateBufferIdListUsingReusableBuffers(renamed_buffers, std::move(buffer_id_list));
    CHECK(buffer_id_list.size() == 6); // NOLINT
    CHECK(buffer_id_list[0] == 0); // NOLINT
    CHECK(buffer_id_list[1] == 1); // NOLINT
    CHECK(buffer_id_list[2] == 7); // NOLINT
    CHECK(buffer_id_list[3] == 2); // NOLINT
    CHECK(buffer_id_list[4] == 3); // NOLINT
    CHECK(buffer_id_list[5] == 5); // NOLINT
    render_pass_buffer_id_list = UpdateRenderPassBufferIdListUsingReusableBuffers(renamed_buffers, std::move(render_pass_buffer_id_list));
    CHECK(render_pass_buffer_id_list[0][0] == 0); // NOLINT
    CHECK(render_pass_buffer_id_list[1][0] == 1); // NOLINT
    CHECK(render_pass_buffer_id_list[1][1] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[2][0] == 1); // NOLINT
    CHECK(render_pass_buffer_id_list[2][1] == 2); // NOLINT
    CHECK(render_pass_buffer_id_list[3][0] == 3); // NOLINT
    CHECK(render_pass_buffer_id_list[3][1] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[4][0] == 3); // NOLINT
    CHECK(render_pass_buffer_id_list[4][1] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[5][0] == 7); // NOLINT
    CHECK(render_pass_buffer_id_list[5][1] == 5); // NOLINT
    CHECK(render_pass_buffer_id_list[6][0] == 5); // NOLINT
    CHECK(render_pass_buffer_id_list[6][1] == 1); // NOLINT
    auto buffer_state_list = CreateBufferStateListBasedOnBufferUserListForTest(buffer_user_pass_list, &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = UpdateBufferStateListAndUserPassListUsingReusableBuffers(renamed_buffers, std::move(buffer_state_list), std::move(buffer_user_pass_list));
    CHECK(buffer_state_list.size() == 6); // NOLINT
    CHECK(!buffer_state_list.contains(4)); // NOLINT
    CHECK(!buffer_state_list.contains(6)); // NOLINT
    CHECK(!buffer_state_list.contains(8)); // NOLINT
    CHECK(buffer_state_list.at(0).size() == 1); // NOLINT
    CHECK(buffer_state_list.at(0)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(1).size() == 3); // NOLINT
    CHECK(buffer_state_list.at(1)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(1)[1] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_state_list.at(1)[2] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(2).size() == 1); // NOLINT
    CHECK(buffer_state_list.at(2)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(3).size() == 2); // NOLINT
    CHECK(buffer_state_list.at(3)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(3)[1] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_state_list.at(5).size() == 2); // NOLINT
    CHECK(buffer_state_list.at(5)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(5)[1] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_state_list.at(7).size() == 4); // NOLINT
    CHECK(buffer_state_list.at(7)[0] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(7)[1] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(7)[2] == kBufferStateFlagCopyDst); // NOLINT
    CHECK(buffer_state_list.at(7)[3] == kBufferStateFlagCopySrc); // NOLINT
    CHECK(buffer_user_pass_list.size() == 6); // NOLINT
    CHECK(!buffer_user_pass_list.contains(4)); // NOLINT
    CHECK(!buffer_user_pass_list.contains(6)); // NOLINT
    CHECK(!buffer_user_pass_list.contains(8)); // NOLINT
    CHECK(buffer_user_pass_list.at(0).size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(0)[0][0] == 0); // NOLINT
    CHECK(buffer_user_pass_list.at(1).size() == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[0][0] == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[1][0] == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(1)[2][0] == 6); // NOLINT
    CHECK(buffer_user_pass_list.at(2).size() == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(2)[0][0] == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(3).size() == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(3)[0][0] == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(3)[1][0] == 4); // NOLINT
    CHECK(buffer_user_pass_list.at(5).size() == 2); // NOLINT
    CHECK(buffer_user_pass_list.at(5)[0][0] == 5); // NOLINT
    CHECK(buffer_user_pass_list.at(5)[1][0] == 6); // NOLINT
    CHECK(buffer_user_pass_list.at(7).size() == 4); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[0][0] == 1); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[1][0] == 3); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[2][0] == 4); // NOLINT
    CHECK(buffer_user_pass_list.at(7)[3][0] == 5); // NOLINT
    for (const auto& [buffer_id, list_list] : buffer_user_pass_list) {
      auto buffer_id_copy = buffer_id;
      CAPTURE(buffer_id_copy);
      for (uint32_t i = 0; i < list_list.size(); i++) {
        CAPTURE(i);
        CHECK(list_list[i].size() == 1); // NOLINT
      }
    }
    unordered_map<BufferId, BufferStateFlags> flag_copy{&memory_resource_work};
    for (const auto& [id, config] : buffer_config_list) {
      flag_copy.insert_or_assign(id, config.state_flags);
    }
    buffer_config_list = MergeReusedBufferConfigs(renamed_buffers, std::move(buffer_config_list));
    CHECK(buffer_config_list.size() == 6); // NOLINT
    CHECK(buffer_config_list.contains(0)); // NOLINT
    CHECK(buffer_config_list.contains(1)); // NOLINT
    CHECK(buffer_config_list.contains(2)); // NOLINT
    CHECK(buffer_config_list.contains(3)); // NOLINT
    CHECK(!buffer_config_list.contains(4)); // NOLINT
    CHECK(buffer_config_list.contains(5)); // NOLINT
    CHECK(!buffer_config_list.contains(6)); // NOLINT
    CHECK(buffer_config_list.contains(7)); // NOLINT
    CHECK(!buffer_config_list.contains(8)); // NOLINT
    for (const auto& [id, config] : buffer_config_list) {
      CHECK(config.state_flags == flag_copy.at(id)); // NOLINT
    }
  }
}
TEST_CASE("memory aliasing with multiple buffers") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  const uint32_t render_pass_num = 5;
  vector<unordered_set<BufferId>> concurrent_buffer_list{&memory_resource_work};
  concurrent_buffer_list.reserve(render_pass_num);
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass0
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass1
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass2
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass3
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass4
  vector<vector<uint32_t>> render_pass_new_buffer_list{&memory_resource_work};
  render_pass_new_buffer_list.reserve(render_pass_num);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass0
  render_pass_new_buffer_list.back().push_back(0);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass1
  render_pass_new_buffer_list.back().push_back(1);
  render_pass_new_buffer_list.back().push_back(2);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass2
  render_pass_new_buffer_list.back().push_back(3);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass3
  render_pass_new_buffer_list.back().push_back(4);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass4
  render_pass_new_buffer_list.back().push_back(5);
  render_pass_new_buffer_list.back().push_back(6);
  vector<vector<uint32_t>> render_pass_expired_buffer_list{&memory_resource_work};
  render_pass_expired_buffer_list.reserve(render_pass_num);
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass0
  render_pass_expired_buffer_list.back().push_back(0);
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass1
  render_pass_expired_buffer_list.back().push_back(2);
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass2
  render_pass_expired_buffer_list.back().push_back(1);
  render_pass_expired_buffer_list.back().push_back(3);
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass3
  render_pass_expired_buffer_list.back().push_back(4);
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass4
  render_pass_expired_buffer_list.back().push_back(5);
  render_pass_expired_buffer_list.back().push_back(6);
  unordered_map<BufferId, BufferConfig> buffer_config_list{&memory_resource_work};
  buffer_config_list.insert_or_assign(0, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(1, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(2, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(3, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(4, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(5, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(6, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.at(1).state_flags = kBufferStateFlagDsv; // alias 0
  buffer_config_list.at(2).width = 10; // alias 0
  buffer_config_list.at(3).width = 10; // reuse 2
  // 4 alias 1
  buffer_config_list.at(5).state_flags = kBufferStateFlagDsv; // disallow reuse 4 to test aliasing 1->5
  // 6 reuse 4
  unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
  buffer_size_list.insert_or_assign(0, 16); // NOLINT
  buffer_size_list.insert_or_assign(1, 8); // NOLINT
  buffer_size_list.insert_or_assign(2, 8); // NOLINT
  buffer_size_list.insert_or_assign(3, 8); // NOLINT
  buffer_size_list.insert_or_assign(4, 4); // NOLINT
  buffer_size_list.insert_or_assign(5, 8); // NOLINT
  buffer_size_list.insert_or_assign(6, 4); // NOLINT
  unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
  buffer_alignment_list.insert_or_assign(0, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(1, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(2, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(3, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(4, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(5, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(6, 4); // NOLINT
  auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(render_pass_num, concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, true, &memory_resource_work, &memory_resource_work);
  CHECK(buffer_address_offset_list.size() == 7); // NOLINT
  CHECK(buffer_address_offset_list.at(0) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(1) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(2) == 8); // NOLINT
  CHECK(buffer_address_offset_list.at(3) == 8); // NOLINT
  CHECK(buffer_address_offset_list.at(4) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(5) == 4); // NOLINT
  CHECK(buffer_address_offset_list.at(6) == 0); // NOLINT
  CHECK(renamed_buffers.size() == 2); // NOLINT
  CHECK(!renamed_buffers.contains(0)); // NOLINT
  CHECK(!renamed_buffers.contains(1)); // NOLINT
  CHECK(!renamed_buffers.contains(2)); // NOLINT
  CHECK(!renamed_buffers.contains(4)); // NOLINT
  CHECK(!renamed_buffers.contains(5)); // NOLINT
  CHECK(renamed_buffers.at(3) == 2); // NOLINT
  CHECK(renamed_buffers.at(6) == 4); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.size() == 3); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(2)); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(1).size() == 2); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(1)[0] == 0); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(1)[1] == 0); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(3).size() == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(3)[0] == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(4).size() == 2); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(4)[0] == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(4)[1] == 3); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.size() == 3); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(2)); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(1).size() == 2); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(1)[0] == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(1)[1] == 2); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(3).size() == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(3)[0] == 4); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(4).size() == 2); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(4)[0] == 5); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(4)[1] == 5); // NOLINT
  concurrent_buffer_list.clear();
  concurrent_buffer_list.reserve(render_pass_num);
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass0
  concurrent_buffer_list.back().insert(1);
  concurrent_buffer_list.back().insert(2);
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass1
  concurrent_buffer_list.back().insert(0);
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass2
  concurrent_buffer_list.back().insert(5);
  concurrent_buffer_list.back().insert(6);
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass3
  concurrent_buffer_list.back().insert(5);
  concurrent_buffer_list.back().insert(6);
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass4
  concurrent_buffer_list.back().insert(1);
  concurrent_buffer_list.back().insert(3);
  concurrent_buffer_list.back().insert(4);
  buffer_config_list.at(3).width = 5; // alias 0 (disallow reuse 2)
  // 4 alias 3
  // 5 alias 0
  buffer_config_list.at(6) = buffer_config_list.at(2);
  buffer_size_list.at(6) = buffer_size_list.at(2); // reuse 2
  std::tie(buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list) = ConfigureBufferAddressOffset(render_pass_num, concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, true, &memory_resource_work, &memory_resource_work);
  CHECK(buffer_address_offset_list.size() == 7); // NOLINT
  CHECK(buffer_address_offset_list.at(0) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(1) == 16); // NOLINT
  CHECK(buffer_address_offset_list.at(2) == 24); // NOLINT
  CHECK(buffer_address_offset_list.at(3) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(4) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(5) == 8); // NOLINT
  CHECK(buffer_address_offset_list.at(6) == 24); // NOLINT
  CHECK(renamed_buffers.size() == 1); // NOLINT
  CHECK(!renamed_buffers.contains(0)); // NOLINT
  CHECK(!renamed_buffers.contains(1)); // NOLINT
  CHECK(!renamed_buffers.contains(2)); // NOLINT
  CHECK(!renamed_buffers.contains(3)); // NOLINT
  CHECK(!renamed_buffers.contains(4)); // NOLINT
  CHECK(!renamed_buffers.contains(5)); // NOLINT
  CHECK(renamed_buffers.at(6) == 2); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.size() == 3); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(1)); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(2).size() == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(2)[0] == 0); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(3).size() == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(3)[0] == 3); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(4).size() == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(4)[0] == 0); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.size() == 3); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(1)); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(2).size() == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(2)[0] == 3); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(3).size() == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(3)[0] == 4); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(4).size() == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(4)[0] == 5); // NOLINT
}
TEST_CASE("memory aliasing in middle of proceding buffer") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  const uint32_t render_pass_num = 3;
  vector<unordered_set<BufferId>> concurrent_buffer_list{&memory_resource_work};
  concurrent_buffer_list.reserve(render_pass_num);
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass0
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass1
  concurrent_buffer_list.push_back(unordered_set<BufferId>{&memory_resource_work}); // pass2
  vector<vector<uint32_t>> render_pass_new_buffer_list{&memory_resource_work};
  render_pass_new_buffer_list.reserve(render_pass_num);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass0
  render_pass_new_buffer_list.back().push_back(0);
  render_pass_new_buffer_list.back().push_back(1);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass1
  render_pass_new_buffer_list.back().push_back(2);
  render_pass_new_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass2
  render_pass_new_buffer_list.back().push_back(3);
  render_pass_new_buffer_list.back().push_back(4);
  vector<vector<uint32_t>> render_pass_expired_buffer_list{&memory_resource_work};
  render_pass_expired_buffer_list.reserve(render_pass_num);
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass0
  render_pass_expired_buffer_list.back().push_back(1);
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass1
  render_pass_expired_buffer_list.push_back(vector<uint32_t>{&memory_resource_work}); // pass2
  render_pass_expired_buffer_list.back().push_back(0);
  render_pass_expired_buffer_list.back().push_back(2);
  render_pass_expired_buffer_list.back().push_back(3);
  render_pass_expired_buffer_list.back().push_back(4);
  unordered_map<BufferId, BufferConfig> buffer_config_list{&memory_resource_work};
  buffer_config_list.insert_or_assign(0, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(1, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(2, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(3, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.insert_or_assign(4, buffer_config_list.at(0)); // NOLINT
  buffer_config_list.at(2).width = 2; // alias 1
  buffer_config_list.at(3).width = 3; // alias 1
  buffer_config_list.at(4).width = 3; // alias 1
  unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
  buffer_size_list.insert_or_assign(0, 4); // NOLINT
  buffer_size_list.insert_or_assign(1, 10); // NOLINT
  buffer_size_list.insert_or_assign(2, 4); // NOLINT
  buffer_size_list.insert_or_assign(3, 4); // NOLINT
  buffer_size_list.insert_or_assign(4, 2); // NOLINT
  unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
  buffer_alignment_list.insert_or_assign(0, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(1, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(2, 8); // NOLINT
  buffer_alignment_list.insert_or_assign(3, 4); // NOLINT
  buffer_alignment_list.insert_or_assign(4, 4); // NOLINT
  auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(render_pass_num, concurrent_buffer_list, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, true, &memory_resource_work, &memory_resource_work);
  CHECK(buffer_address_offset_list.size() == 5); // NOLINT
  CHECK(buffer_address_offset_list.at(0) == 0); // NOLINT
  CHECK(buffer_address_offset_list.at(1) == 4); // NOLINT
  CHECK(buffer_address_offset_list.at(2) == 8); // NOLINT
  CHECK(buffer_address_offset_list.at(3) == 4); // NOLINT
  CHECK(buffer_address_offset_list.at(4) == 12); // NOLINT
  CHECK(renamed_buffers.empty());
  CHECK(!renamed_buffers.contains(0)); // NOLINT
  CHECK(!renamed_buffers.contains(1)); // NOLINT
  CHECK(!renamed_buffers.contains(2)); // NOLINT
  CHECK(!renamed_buffers.contains(3)); // NOLINT
  CHECK(!renamed_buffers.contains(4)); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.size() == 2); // NOLINT
  CHECK(!render_pass_before_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(1).size() == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(1)[0] == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(2).size() == 2); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(2)[0] == 1); // NOLINT
  CHECK(render_pass_before_memory_aliasing_list.at(2)[1] == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.size() == 2); // NOLINT
  CHECK(!render_pass_after_memory_aliasing_list.contains(0)); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(1).size() == 1); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(1)[0] == 2); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(2).size() == 2); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(2)[0] == 3); // NOLINT
  CHECK(render_pass_after_memory_aliasing_list.at(2)[1] == 4); // NOLINT
}
TEST_CASE("UpdateMemoryAliasingListWithRenamedBufferIdList") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<uint32_t, vector<BufferId>> render_pass_memory_aliasing_list{&memory_resource_work};
  render_pass_memory_aliasing_list.insert_or_assign(1, vector<BufferId>{&memory_resource_work});
  render_pass_memory_aliasing_list.at(1).push_back(0);
  render_pass_memory_aliasing_list.at(1).push_back(1);
  render_pass_memory_aliasing_list.insert_or_assign(3, vector<BufferId>{&memory_resource_work});
  render_pass_memory_aliasing_list.at(3).push_back(1);
  render_pass_memory_aliasing_list.at(3).push_back(2);
  render_pass_memory_aliasing_list.insert_or_assign(8, vector<BufferId>{&memory_resource_work});
  render_pass_memory_aliasing_list.at(8).push_back(4);
  render_pass_memory_aliasing_list.at(8).push_back(5);
  unordered_map<BufferId, BufferId> renamed_buffers{&memory_resource_work};
  renamed_buffers.insert_or_assign(2, 0);
  renamed_buffers.insert_or_assign(4, 0);
  renamed_buffers.insert_or_assign(5, 1);
  render_pass_memory_aliasing_list = UpdateMemoryAliasingListWithRenamedBufferIdList(renamed_buffers, std::move(render_pass_memory_aliasing_list));
  CHECK(render_pass_memory_aliasing_list.size() == 3); // NOLINT
  CHECK(render_pass_memory_aliasing_list.at(1)[0] == 0); // NOLINT
  CHECK(render_pass_memory_aliasing_list.at(1)[1] == 1); // NOLINT
  CHECK(render_pass_memory_aliasing_list.at(3)[0] == 1); // NOLINT
  CHECK(render_pass_memory_aliasing_list.at(3)[1] == 0); // NOLINT
  CHECK(render_pass_memory_aliasing_list.at(8)[0] == 0); // NOLINT
  CHECK(render_pass_memory_aliasing_list.at(8)[1] == 1); // NOLINT
}
TEST_CASE("AppendMemoryAliasingBarriers") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  const uint32_t pass_num = 3;
  unordered_map<uint32_t, vector<BufferId>> render_pass_before_memory_aliasing_list{&memory_resource_work};
  unordered_map<uint32_t, vector<BufferId>> render_pass_after_memory_aliasing_list{&memory_resource_work};
  render_pass_before_memory_aliasing_list.insert_or_assign(0, vector<BufferId>{&memory_resource_work});
  render_pass_after_memory_aliasing_list.insert_or_assign(0, vector<BufferId>{&memory_resource_work});
  render_pass_before_memory_aliasing_list.at(0).push_back(0);
  render_pass_after_memory_aliasing_list.at(0).push_back(1);
  render_pass_before_memory_aliasing_list.at(0).push_back(2);
  render_pass_after_memory_aliasing_list.at(0).push_back(3);
  render_pass_before_memory_aliasing_list.insert_or_assign(1, vector<BufferId>{&memory_resource_work});
  render_pass_after_memory_aliasing_list.insert_or_assign(1, vector<BufferId>{&memory_resource_work});
  render_pass_before_memory_aliasing_list.at(1).push_back(4);
  render_pass_after_memory_aliasing_list.at(1).push_back(5);
  render_pass_before_memory_aliasing_list.insert_or_assign(2, vector<BufferId>{&memory_resource_work});
  render_pass_after_memory_aliasing_list.insert_or_assign(2, vector<BufferId>{&memory_resource_work});
  render_pass_before_memory_aliasing_list.at(2).push_back(6);
  render_pass_after_memory_aliasing_list.at(2).push_back(7);
  render_pass_before_memory_aliasing_list.at(2).push_back(8);
  render_pass_after_memory_aliasing_list.at(2).push_back(9);
  render_pass_before_memory_aliasing_list.at(2).push_back(10);
  render_pass_after_memory_aliasing_list.at(2).push_back(11);
  vector<vector<BarrierConfig>> barriers_pre_pass{&memory_resource_work};
  barriers_pre_pass.push_back(vector<BarrierConfig>{&memory_resource_work}); // pass0
  barriers_pre_pass.push_back(vector<BarrierConfig>{&memory_resource_work}); // pass1
  barriers_pre_pass.push_back(vector<BarrierConfig>{&memory_resource_work}); // pass2
  barriers_pre_pass.back().push_back(BarrierConfig{.buffer_id=101,.split_type=BarrierSplitType::kBegin,.params=BarrierTransition{.state_before=kBufferStateFlagRtv,.state_after=kBufferStateFlagSrvPsOnly}});
  barriers_pre_pass = AppendMemoryAliasingBarriers(pass_num, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list, std::move(barriers_pre_pass));
  CHECK(barriers_pre_pass.size() == 3); // NOLINT
  CHECK(barriers_pre_pass[0].size() == 2); // NOLINT
  CHECK(barriers_pre_pass[0][0].buffer_id == 0); // NOLINT
  CHECK(std::get<BarrierAliasing>(barriers_pre_pass[0][0].params).buffer_id_after_aliasing == 1); // NOLINT
  CHECK(barriers_pre_pass[0][1].buffer_id == 2); // NOLINT
  CHECK(std::get<BarrierAliasing>(barriers_pre_pass[0][1].params).buffer_id_after_aliasing == 3); // NOLINT
  CHECK(barriers_pre_pass[1].size() == 1); // NOLINT
  CHECK(barriers_pre_pass[1][0].buffer_id == 4); // NOLINT
  CHECK(std::get<BarrierAliasing>(barriers_pre_pass[1][0].params).buffer_id_after_aliasing == 5); // NOLINT
  CHECK(barriers_pre_pass[2].size() == 4); // NOLINT
  CHECK(barriers_pre_pass[2][0].buffer_id == 101); // NOLINT
  CHECK(barriers_pre_pass[2][0].split_type == BarrierSplitType::kBegin); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_pre_pass[2][0].params).state_before == kBufferStateFlagRtv); // NOLINT
  CHECK(std::get<BarrierTransition>(barriers_pre_pass[2][0].params).state_after == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(barriers_pre_pass[2][1].buffer_id == 6); // NOLINT
  CHECK(std::get<BarrierAliasing>(barriers_pre_pass[2][1].params).buffer_id_after_aliasing == 7); // NOLINT
  CHECK(barriers_pre_pass[2][2].buffer_id == 8); // NOLINT
  CHECK(std::get<BarrierAliasing>(barriers_pre_pass[2][2].params).buffer_id_after_aliasing == 9); // NOLINT
  CHECK(barriers_pre_pass[2][3].buffer_id == 10); // NOLINT
  CHECK(std::get<BarrierAliasing>(barriers_pre_pass[2][3].params).buffer_id_after_aliasing == 11); // NOLINT
}
TEST_CASE("UpdateRenderPassBufferClearInfoWithRenamedBufferIdList") {
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<BufferId, BufferId> renamed_buffers{&memory_resource_work};
  renamed_buffers.insert_or_assign(1, 0);
  renamed_buffers.insert_or_assign(2, 0);
  renamed_buffers.insert_or_assign(5, 4);
  vector<unordered_map<BufferId, BufferClearType>> render_pass_buffer_clear_info{&memory_resource_work};
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 0
  render_pass_buffer_clear_info.back().insert_or_assign(0, BufferClearType::kClear);
  render_pass_buffer_clear_info.back().insert_or_assign(3, BufferClearType::kDontCare);
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 1
  render_pass_buffer_clear_info.back().insert_or_assign(1, BufferClearType::kClear);
  render_pass_buffer_clear_info.back().insert_or_assign(3, BufferClearType::kDontCare);
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 2
  render_pass_buffer_clear_info.back().insert_or_assign(2, BufferClearType::kDontCare);
  render_pass_buffer_clear_info.back().insert_or_assign(5, BufferClearType::kClear);
  render_pass_buffer_clear_info.back().insert_or_assign(6, BufferClearType::kDontCare);
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 3
  render_pass_buffer_clear_info = UpdateRenderPassBufferClearInfoWithRenamedBufferIdList(renamed_buffers, std::move(render_pass_buffer_clear_info));
  CHECK(render_pass_buffer_clear_info.size() == 4); // NOLINT
  CHECK(render_pass_buffer_clear_info[0].size() == 2); // NOLINT
  CHECK(render_pass_buffer_clear_info[0].at(0) == BufferClearType::kClear); // NOLINT
  CHECK(render_pass_buffer_clear_info[0].at(3) == BufferClearType::kDontCare); // NOLINT
  CHECK(render_pass_buffer_clear_info[1].size() == 2); // NOLINT
  CHECK(render_pass_buffer_clear_info[1].at(0) == BufferClearType::kClear); // NOLINT
  CHECK(render_pass_buffer_clear_info[1].at(3) == BufferClearType::kDontCare); // NOLINT
  CHECK(render_pass_buffer_clear_info[2].size() == 3); // NOLINT
  CHECK(render_pass_buffer_clear_info[2].at(0) == BufferClearType::kDontCare); // NOLINT
  CHECK(render_pass_buffer_clear_info[2].at(4) == BufferClearType::kClear); // NOLINT
  CHECK(render_pass_buffer_clear_info[2].at(6) == BufferClearType::kDontCare); // NOLINT
  CHECK(render_pass_buffer_clear_info[3].empty()); // NOLINT
}
TEST_CASE("AppendBufferClearInfoCausedByMemoryAliasing") {
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<uint32_t, vector<BufferId>> render_pass_after_memory_aliasing_list{&memory_resource_work};
  render_pass_after_memory_aliasing_list.insert_or_assign(1, vector<BufferId>{&memory_resource_work});
  render_pass_after_memory_aliasing_list.at(1).push_back(2);
  render_pass_after_memory_aliasing_list.insert_or_assign(3, vector<BufferId>{&memory_resource_work});
  render_pass_after_memory_aliasing_list.at(3).push_back(3);
  vector<unordered_map<BufferId, BufferClearType>> render_pass_buffer_clear_info{&memory_resource_work};
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 0
  render_pass_buffer_clear_info.back().insert_or_assign(0, BufferClearType::kClear);
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 1
  render_pass_buffer_clear_info.back().insert_or_assign(1, BufferClearType::kClear);
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 2
  render_pass_buffer_clear_info.push_back(unordered_map<BufferId, BufferClearType>{&memory_resource_work}); // pass 3
  render_pass_buffer_clear_info = AppendBufferClearInfoCausedByMemoryAliasing(4, render_pass_after_memory_aliasing_list, std::move(render_pass_buffer_clear_info));
  CHECK(render_pass_buffer_clear_info.size() == 4); // NOLINT
  CHECK(render_pass_buffer_clear_info[0].size() == 1); // NOLINT
  CHECK(render_pass_buffer_clear_info[0].at(0) == BufferClearType::kClear); // NOLINT
  CHECK(render_pass_buffer_clear_info[1].size() == 2); // NOLINT
  CHECK(render_pass_buffer_clear_info[1].at(1) == BufferClearType::kClear); // NOLINT
  CHECK(render_pass_buffer_clear_info[1].at(2) == BufferClearType::kDontCare); // NOLINT
  CHECK(render_pass_buffer_clear_info[2].empty()); // NOLINT
  CHECK(render_pass_buffer_clear_info[3].size() == 1); // NOLINT
  CHECK(render_pass_buffer_clear_info[3].at(3) == BufferClearType::kDontCare); // NOLINT
}
TEST_CASE("buffer allocation address calc") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<uint32_t> used_range_start{&memory_resource_work};
  vector<uint32_t> used_range_end{&memory_resource_work};
  auto offset = RetainMemory(4, 8, &used_range_start, &used_range_end); // NOLINT
  CHECK(offset == 0); // NOLINT
  CHECK(used_range_start.size() == 1); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_end.size() == 1); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  offset = RetainMemory(4, 8, &used_range_start, &used_range_end); // NOLINT
  CHECK(offset == 8); // NOLINT
  CHECK(used_range_start.size() == 2); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_start[1] == 8); // NOLINT
  CHECK(used_range_end.size() == 2); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  CHECK(used_range_end[1] == 12); // NOLINT
  offset = RetainMemory(5, 4, &used_range_start, &used_range_end); // NOLINT
  CHECK(offset == 12); // NOLINT
  CHECK(used_range_start.size() == 3); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_start[1] == 8); // NOLINT
  CHECK(used_range_start[2] == 12); // NOLINT
  CHECK(used_range_end.size() == 3); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  CHECK(used_range_end[1] == 12); // NOLINT
  CHECK(used_range_end[2] == 17); // NOLINT
  ReturnMemory(8, &used_range_start, &used_range_end); // NOLINT
  CHECK(used_range_start.size() == 2); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_start[1] == 12); // NOLINT
  CHECK(used_range_end.size() == 2); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  CHECK(used_range_end[1] == 17); // NOLINT
  offset = RetainMemory(2, 8, &used_range_start, &used_range_end); // NOLINT
  CHECK(offset == 8); // NOLINT
  CHECK(used_range_start.size() == 3); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_start[1] == 8); // NOLINT
  CHECK(used_range_start[2] == 12); // NOLINT
  CHECK(used_range_end.size() == 3); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  CHECK(used_range_end[1] == 10); // NOLINT
  CHECK(used_range_end[2] == 17); // NOLINT
  ReturnMemory(8, &used_range_start, &used_range_end); // NOLINT
  CHECK(used_range_start.size() == 2); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_start[1] == 12); // NOLINT
  CHECK(used_range_end.size() == 2); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  CHECK(used_range_end[1] == 17); // NOLINT
  ReturnMemory(0, &used_range_start, &used_range_end); // NOLINT
  CHECK(used_range_start.size() == 1); // NOLINT
  CHECK(used_range_start[0] == 12); // NOLINT
  CHECK(used_range_end.size() == 1); // NOLINT
  CHECK(used_range_end[0] == 17); // NOLINT
  offset = RetainMemory(4, 8, &used_range_start, &used_range_end); // NOLINT
  CHECK(offset == 0); // NOLINT
  CHECK(used_range_start.size() == 2); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_start[1] == 12); // NOLINT
  CHECK(used_range_end.size() == 2); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  CHECK(used_range_end[1] == 17); // NOLINT
  offset = RetainMemory(8, 4, &used_range_start, &used_range_end); // NOLINT
  CHECK(offset == 4); // NOLINT
  CHECK(used_range_start.size() == 3); // NOLINT
  CHECK(used_range_start[0] == 0); // NOLINT
  CHECK(used_range_start[1] == 4); // NOLINT
  CHECK(used_range_start[2] == 12); // NOLINT
  CHECK(used_range_end.size() == 3); // NOLINT
  CHECK(used_range_end[0] == 4); // NOLINT
  CHECK(used_range_end[1] == 12); // NOLINT
  CHECK(used_range_end[2] == 17); // NOLINT
}
TEST_CASE("pass culling") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<BufferId, vector<BufferStateFlags>> buffer_state_list{&memory_resource_work};
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  unordered_set<BufferId> required_buffers{&memory_resource_work};
  BufferId buffer_id = 0;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(0);
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagSrvPsOnly);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(2);
  buffer_id = 1;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(1);
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagSrvPsOnly);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(2);
  buffer_id = 2;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(2);
  auto [render_pass_buffer_id_list, render_pass_buffer_state_flag_list] = CreateRenderPassBufferInfoForTest(buffer_state_list, buffer_user_pass_list, &memory_resource_work);
  required_buffers.insert(0);
  auto [used_pass_list, used_buffer_list] = CullUsedRenderPass(buffer_state_list, buffer_user_pass_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, std::move(required_buffers), &memory_resource_work, &memory_resource_work);
  CHECK(used_pass_list.size() == 1); // NOLINT
  CHECK(used_pass_list.contains(0)); // NOLINT
  CHECK(!used_pass_list.contains(1)); // NOLINT
  CHECK(!used_pass_list.contains(2)); // NOLINT
  CHECK(used_buffer_list.size() == 1); // NOLINT
  CHECK(used_buffer_list.contains(0)); // NOLINT
  CHECK(!used_buffer_list.contains(1)); // NOLINT
  CHECK(!used_buffer_list.contains(2)); // NOLINT
  required_buffers.clear();
  required_buffers.insert(1);
  std::tie(used_pass_list, used_buffer_list) = CullUsedRenderPass(buffer_state_list, buffer_user_pass_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, std::move(required_buffers), &memory_resource_work, &memory_resource_work);
  CHECK(used_pass_list.size() == 1); // NOLINT
  CHECK(!used_pass_list.contains(0)); // NOLINT
  CHECK(used_pass_list.contains(1)); // NOLINT
  CHECK(!used_pass_list.contains(2)); // NOLINT
  CHECK(used_buffer_list.size() == 1); // NOLINT
  CHECK(!used_buffer_list.contains(0)); // NOLINT
  CHECK(used_buffer_list.contains(1)); // NOLINT
  CHECK(!used_buffer_list.contains(2)); // NOLINT
  required_buffers.clear();
  required_buffers.insert(2);
  std::tie(used_pass_list, used_buffer_list) = CullUsedRenderPass(buffer_state_list, buffer_user_pass_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, std::move(required_buffers), &memory_resource_work, &memory_resource_work);
  CHECK(used_pass_list.size() == 3); // NOLINT
  CHECK(used_pass_list.contains(0)); // NOLINT
  CHECK(used_pass_list.contains(1)); // NOLINT
  CHECK(used_pass_list.contains(2)); // NOLINT
  CHECK(used_buffer_list.size() == 3); // NOLINT
  CHECK(used_buffer_list.contains(0)); // NOLINT
  CHECK(used_buffer_list.contains(1)); // NOLINT
  CHECK(used_buffer_list.contains(2)); // NOLINT
  buffer_id = 2;
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagSrvPsOnly);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(4);
  buffer_id = 3;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(3);
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagSrvPsOnly);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(4);
  buffer_id = 4;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(4);
  required_buffers.clear();
  required_buffers.insert(4);
  std::tie(render_pass_buffer_id_list, render_pass_buffer_state_flag_list) = CreateRenderPassBufferInfoForTest(buffer_state_list, buffer_user_pass_list, &memory_resource_work);
  std::tie(used_pass_list, used_buffer_list) = CullUsedRenderPass(buffer_state_list, buffer_user_pass_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, std::move(required_buffers), &memory_resource_work, &memory_resource_work);
  CHECK(used_pass_list.size() == 5); // NOLINT
  CHECK(used_pass_list.contains(0)); // NOLINT
  CHECK(used_pass_list.contains(1)); // NOLINT
  CHECK(used_pass_list.contains(2)); // NOLINT
  CHECK(used_pass_list.contains(3)); // NOLINT
  CHECK(used_pass_list.contains(4)); // NOLINT
  CHECK(used_buffer_list.size() == 5); // NOLINT
  CHECK(used_buffer_list.contains(0)); // NOLINT
  CHECK(used_buffer_list.contains(1)); // NOLINT
  CHECK(used_buffer_list.contains(2)); // NOLINT
  CHECK(used_buffer_list.contains(3)); // NOLINT
  CHECK(used_buffer_list.contains(4)); // NOLINT
  buffer_id = 3;
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagSrvPsOnly);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(5);
  buffer_id = 5;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(5);
  required_buffers.clear();
  required_buffers.insert(2);
  required_buffers.insert(5);
  std::tie(render_pass_buffer_id_list, render_pass_buffer_state_flag_list) = CreateRenderPassBufferInfoForTest(buffer_state_list, buffer_user_pass_list, &memory_resource_work);
  std::tie(used_pass_list, used_buffer_list) = CullUsedRenderPass(buffer_state_list, buffer_user_pass_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, std::move(required_buffers), &memory_resource_work, &memory_resource_work);
  CHECK(used_pass_list.size() == 5); // NOLINT
  CHECK(used_pass_list.contains(0)); // NOLINT
  CHECK(used_pass_list.contains(1)); // NOLINT
  CHECK(used_pass_list.contains(2)); // NOLINT
  CHECK(used_pass_list.contains(3)); // NOLINT
  CHECK(!used_pass_list.contains(4)); // NOLINT
  CHECK(used_pass_list.contains(5)); // NOLINT
  CHECK(used_buffer_list.size() == 5); // NOLINT
  CHECK(used_buffer_list.contains(0)); // NOLINT
  CHECK(used_buffer_list.contains(1)); // NOLINT
  CHECK(used_buffer_list.contains(2)); // NOLINT
  CHECK(used_buffer_list.contains(3)); // NOLINT
  CHECK(!used_buffer_list.contains(4)); // NOLINT
  CHECK(used_buffer_list.contains(5)); // NOLINT
  required_buffers.clear();
  required_buffers.insert(4);
  required_buffers.insert(5);
  std::tie(used_pass_list, used_buffer_list) = CullUsedRenderPass(buffer_state_list, buffer_user_pass_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, std::move(required_buffers), &memory_resource_work, &memory_resource_work);
  CHECK(used_pass_list.size() == 6); // NOLINT
  CHECK(used_pass_list.contains(0)); // NOLINT
  CHECK(used_pass_list.contains(1)); // NOLINT
  CHECK(used_pass_list.contains(2)); // NOLINT
  CHECK(used_pass_list.contains(3)); // NOLINT
  CHECK(used_pass_list.contains(4)); // NOLINT
  CHECK(used_pass_list.contains(5)); // NOLINT
  CHECK(used_buffer_list.size() == 6); // NOLINT
  CHECK(used_buffer_list.contains(0)); // NOLINT
  CHECK(used_buffer_list.contains(1)); // NOLINT
  CHECK(used_buffer_list.contains(2)); // NOLINT
  CHECK(used_buffer_list.contains(3)); // NOLINT
  CHECK(used_buffer_list.contains(4)); // NOLINT
  CHECK(used_buffer_list.contains(5)); // NOLINT
}
TEST_CASE("CalculateNewPassIndexAfterPassCulling") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  uint32_t render_pass_num = 5;
  unordered_set<uint32_t> used_render_pass_list{&memory_resource_work};
  used_render_pass_list.insert(0);
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(3);
  used_render_pass_list.insert(4);
  auto new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.size() == 3); // NOLINT
  CHECK(!new_render_pass_index_list.contains(0)); // NOLINT
  CHECK(!new_render_pass_index_list.contains(1)); // NOLINT
  CHECK(new_render_pass_index_list.at(2) == 1); // NOLINT
  CHECK(new_render_pass_index_list.at(3) == 2); // NOLINT
  CHECK(new_render_pass_index_list.at(4) == 3); // NOLINT
  used_render_pass_list.clear();
  used_render_pass_list.insert(1);
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(3);
  used_render_pass_list.insert(4);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.size() == 4); // NOLINT
  CHECK(!new_render_pass_index_list.contains(0)); // NOLINT
  CHECK(new_render_pass_index_list.at(1) == 0); // NOLINT
  CHECK(new_render_pass_index_list.at(2) == 1); // NOLINT
  CHECK(new_render_pass_index_list.at(3) == 2); // NOLINT
  CHECK(new_render_pass_index_list.at(4) == 3); // NOLINT
  used_render_pass_list.clear();
  used_render_pass_list.insert(0);
  used_render_pass_list.insert(1);
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(3);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.empty()); // NOLINT
  used_render_pass_list.clear();
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(3);
  used_render_pass_list.insert(4);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.size() == 3); // NOLINT
  CHECK(!new_render_pass_index_list.contains(0)); // NOLINT
  CHECK(!new_render_pass_index_list.contains(1)); // NOLINT
  CHECK(new_render_pass_index_list.at(2) == 0); // NOLINT
  CHECK(new_render_pass_index_list.at(3) == 1); // NOLINT
  CHECK(new_render_pass_index_list.at(4) == 2); // NOLINT
  used_render_pass_list.clear();
  used_render_pass_list.insert(1);
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(4);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.size() == 3); // NOLINT
  CHECK(!new_render_pass_index_list.contains(0)); // NOLINT
  CHECK(new_render_pass_index_list.at(1) == 0); // NOLINT
  CHECK(new_render_pass_index_list.at(2) == 1); // NOLINT
  CHECK(!new_render_pass_index_list.contains(3)); // NOLINT
  CHECK(new_render_pass_index_list.at(4) == 2); // NOLINT
  used_render_pass_list.clear();
  used_render_pass_list.insert(1);
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(3);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.size() == 3); // NOLINT
  CHECK(!new_render_pass_index_list.contains(0)); // NOLINT
  CHECK(new_render_pass_index_list.at(1) == 0); // NOLINT
  CHECK(new_render_pass_index_list.at(2) == 1); // NOLINT
  CHECK(new_render_pass_index_list.at(3) == 2); // NOLINT
  CHECK(!new_render_pass_index_list.contains(4)); // NOLINT
  used_render_pass_list.clear();
  used_render_pass_list.insert(0);
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(4);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.size() == 2); // NOLINT
  CHECK(!new_render_pass_index_list.contains(0)); // NOLINT
  CHECK(!new_render_pass_index_list.contains(1)); // NOLINT
  CHECK(new_render_pass_index_list.at(2) == 1); // NOLINT
  CHECK(!new_render_pass_index_list.contains(3)); // NOLINT
  CHECK(new_render_pass_index_list.at(4) == 2); // NOLINT
  used_render_pass_list.clear();
  used_render_pass_list.insert(0);
  used_render_pass_list.insert(1);
  used_render_pass_list.insert(2);
  used_render_pass_list.insert(3);
  used_render_pass_list.insert(4);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_render_pass_list, &memory_resource_work);
  CHECK(new_render_pass_index_list.empty()); // NOLINT
}
TEST_CASE("update params after pass culling") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<BufferId, vector<BufferStateFlags>> buffer_state_list{&memory_resource_work};
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  BufferId buffer_id = 0;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(0);
  buffer_user_pass_list.at(buffer_id).back().push_back(1);
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagSrvPsOnly);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(2);
  buffer_user_pass_list.at(buffer_id).back().push_back(3);
  buffer_user_pass_list.at(buffer_id).back().push_back(4);
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagUavRW);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(5);
  buffer_id = 1;
  buffer_state_list.insert_or_assign(buffer_id, vector<BufferStateFlags>{&memory_resource_work});
  buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagRtv);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(6);
  buffer_state_list.at(buffer_id).push_back(kBufferStateFlagSrvPsOnly);
  buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(buffer_id).back().push_back(7);
  unordered_set<BufferId> used_buffer_list{&memory_resource_work};
  unordered_set<BufferId> used_pass_list{&memory_resource_work};
  used_buffer_list.insert(0);
  used_buffer_list.insert(1);
  used_pass_list.insert(0);
  used_pass_list.insert(1);
  used_pass_list.insert(2);
  used_pass_list.insert(3);
  used_pass_list.insert(4);
  used_pass_list.insert(5);
  used_pass_list.insert(6);
  used_pass_list.insert(7);
  uint32_t render_pass_num = 8;
  auto new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_pass_list, &memory_resource_work);
  auto buffer_state_list_copy = buffer_state_list;
  auto buffer_user_pass_list_copy = buffer_user_pass_list;
  std::tie(buffer_state_list_copy, buffer_user_pass_list_copy) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list_copy), std::move(buffer_user_pass_list_copy));
  CHECK(buffer_state_list_copy.size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0).size() == 3); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[2] == kBufferStateFlagUavRW); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0).size() == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0].size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][0] == 0); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][1] == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1].size() == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][0] == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][1] == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][2] == 4); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[2].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[2][0] == 5); // NOLINT
  CHECK(buffer_state_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0][0] == 6); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1][0] == 7); // NOLINT
  used_buffer_list.clear();
  used_buffer_list.insert(0);
  used_pass_list.clear();
  used_pass_list.insert(1);
  used_pass_list.insert(2);
  used_pass_list.insert(3);
  used_pass_list.insert(4);
  used_pass_list.insert(5);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_pass_list, &memory_resource_work);
  buffer_state_list_copy = buffer_state_list;
  buffer_user_pass_list_copy = buffer_user_pass_list;
  std::tie(buffer_state_list_copy, buffer_user_pass_list_copy) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list_copy), std::move(buffer_user_pass_list_copy));
  CHECK(buffer_state_list_copy.size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.size() == 1); // NOLINT
  CHECK(buffer_state_list_copy.at(0).size() == 3); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[2] == kBufferStateFlagUavRW); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0).size() == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][0] == 0); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1].size() == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][0] == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][1] == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][2] == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[2].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[2][0] == 4); // NOLINT
  used_buffer_list.clear();
  used_buffer_list.insert(0);
  used_buffer_list.insert(1);
  used_pass_list.clear();
  used_pass_list.insert(2);
  used_pass_list.insert(3);
  used_pass_list.insert(4);
  used_pass_list.insert(5);
  used_pass_list.insert(6);
  used_pass_list.insert(7);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_pass_list, &memory_resource_work);
  buffer_state_list_copy = buffer_state_list;
  buffer_user_pass_list_copy = buffer_user_pass_list;
  std::tie(buffer_state_list_copy, buffer_user_pass_list_copy) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list_copy), std::move(buffer_user_pass_list_copy));
  CHECK(buffer_state_list_copy.size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[0] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[1] == kBufferStateFlagUavRW); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0].size() == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][0] == 0); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][1] == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][2] == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][0] == 3); // NOLINT
  CHECK(buffer_state_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0][0] == 4); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1][0] == 5); // NOLINT
  used_buffer_list.clear();
  used_buffer_list.insert(0);
  used_buffer_list.insert(1);
  used_pass_list.clear();
  used_pass_list.insert(0);
  used_pass_list.insert(1);
  used_pass_list.insert(2);
  used_pass_list.insert(4);
  used_pass_list.insert(5);
  used_pass_list.insert(6);
  used_pass_list.insert(7);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_pass_list, &memory_resource_work);
  buffer_state_list_copy = buffer_state_list;
  buffer_user_pass_list_copy = buffer_user_pass_list;
  std::tie(buffer_state_list_copy, buffer_user_pass_list_copy) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list_copy), std::move(buffer_user_pass_list_copy));
  CHECK(buffer_state_list_copy.size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0).size() == 3); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[2] == kBufferStateFlagUavRW); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0).size() == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0].size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][0] == 0); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][1] == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1].size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][0] == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][1] == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[2].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[2][0] == 4); // NOLINT
  CHECK(buffer_state_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0][0] == 5); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1][0] == 6); // NOLINT
  used_buffer_list.clear();
  used_buffer_list.insert(0);
  used_buffer_list.insert(1);
  used_pass_list.clear();
  used_pass_list.insert(0);
  used_pass_list.insert(1);
  used_pass_list.insert(5);
  used_pass_list.insert(6);
  used_pass_list.insert(7);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_pass_list, &memory_resource_work);
  buffer_state_list_copy = buffer_state_list;
  buffer_user_pass_list_copy = buffer_user_pass_list;
  std::tie(buffer_state_list_copy, buffer_user_pass_list_copy) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list_copy), std::move(buffer_user_pass_list_copy));
  CHECK(buffer_state_list_copy.size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[1] == kBufferStateFlagUavRW); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0].size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][0] == 0); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][1] == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][0] == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0][0] == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1][0] == 4); // NOLINT
  used_buffer_list.clear();
  used_buffer_list.insert(0);
  used_buffer_list.insert(1);
  used_pass_list.clear();
  used_pass_list.insert(0);
  used_pass_list.insert(1);
  used_pass_list.insert(2);
  used_pass_list.insert(3);
  used_pass_list.insert(4);
  used_pass_list.insert(6);
  used_pass_list.insert(7);
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_pass_list, &memory_resource_work);
  buffer_state_list_copy = buffer_state_list;
  buffer_user_pass_list_copy = buffer_user_pass_list;
  std::tie(buffer_state_list_copy, buffer_user_pass_list_copy) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list_copy), std::move(buffer_user_pass_list_copy));
  CHECK(buffer_state_list_copy.size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(0)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0].size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][0] == 0); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[0][1] == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1].size() == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][0] == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][1] == 3); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(0)[1][2] == 4); // NOLINT
  CHECK(buffer_state_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[0] == kBufferStateFlagRtv); // NOLINT
  CHECK(buffer_state_list_copy.at(1)[1] == kBufferStateFlagSrvPsOnly); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1).size() == 2); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[0][0] == 5); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1].size() == 1); // NOLINT
  CHECK(buffer_user_pass_list_copy.at(1)[1][0] == 6); // NOLINT
  used_buffer_list.clear();
  used_pass_list.clear();
  new_render_pass_index_list = CalculateNewPassIndexAfterPassCulling(render_pass_num, used_pass_list, &memory_resource_work);
  buffer_state_list_copy = buffer_state_list;
  buffer_user_pass_list_copy = buffer_user_pass_list;
  std::tie(buffer_state_list_copy, buffer_user_pass_list_copy) = UpdateBufferStateInfoWithNewPassIndex(used_pass_list, used_buffer_list, new_render_pass_index_list, std::move(buffer_state_list_copy), std::move(buffer_user_pass_list_copy));
  CHECK(buffer_state_list_copy.empty()); // NOLINT
  CHECK(buffer_user_pass_list_copy.empty()); // NOLINT
  vector<CommandQueueType> src_list{&memory_resource_work};
  src_list.push_back(CommandQueueType::kGraphics);
  src_list.push_back(CommandQueueType::kCompute);
  src_list.push_back(CommandQueueType::kTransfer);
  new_render_pass_index_list.clear();
  auto dst_list = GetRenderCommandQueueTypeListWithNewPassIndex(3, new_render_pass_index_list, src_list, &memory_resource_work, &memory_resource_work);
  CHECK(dst_list.size() == 3); // NOLINT
  CHECK(dst_list[0] == CommandQueueType::kGraphics); // NOLINT
  CHECK(dst_list[1] == CommandQueueType::kCompute); // NOLINT
  CHECK(dst_list[2] == CommandQueueType::kTransfer); // NOLINT
  dst_list = GetRenderCommandQueueTypeListWithNewPassIndex(2, new_render_pass_index_list, src_list, &memory_resource_work, &memory_resource_work);
  CHECK(dst_list.size() == 2); // NOLINT
  CHECK(dst_list[0] == CommandQueueType::kGraphics); // NOLINT
  CHECK(dst_list[1] == CommandQueueType::kCompute); // NOLINT
  new_render_pass_index_list.clear();
  new_render_pass_index_list.insert_or_assign(1, 0);
  new_render_pass_index_list.insert_or_assign(2, 1);
  dst_list = GetRenderCommandQueueTypeListWithNewPassIndex(2, new_render_pass_index_list, src_list, &memory_resource_work, &memory_resource_work);
  CHECK(dst_list.size() == 2); // NOLINT
  CHECK(dst_list[0] == CommandQueueType::kCompute); // NOLINT
  CHECK(dst_list[1] == CommandQueueType::kTransfer); // NOLINT
  new_render_pass_index_list.clear();
  new_render_pass_index_list.insert_or_assign(2, 1);
  dst_list = GetRenderCommandQueueTypeListWithNewPassIndex(2, new_render_pass_index_list, src_list, &memory_resource_work, &memory_resource_work);
  CHECK(dst_list.size() == 2); // NOLINT
  CHECK(dst_list[0] == CommandQueueType::kGraphics); // NOLINT
  CHECK(dst_list[1] == CommandQueueType::kTransfer); // NOLINT
  new_render_pass_index_list.clear();
  new_render_pass_index_list.insert_or_assign(2, 0);
  dst_list = GetRenderCommandQueueTypeListWithNewPassIndex(1, new_render_pass_index_list, src_list, &memory_resource_work, &memory_resource_work);
  CHECK(dst_list.size() == 1); // NOLINT
  CHECK(dst_list[0] == CommandQueueType::kTransfer); // NOLINT
  new_render_pass_index_list.clear();
  dst_list = GetRenderCommandQueueTypeListWithNewPassIndex(1, new_render_pass_index_list, src_list, &memory_resource_work, &memory_resource_work);
  CHECK(dst_list.size() == 1); // NOLINT
  CHECK(dst_list[0] == CommandQueueType::kGraphics); // NOLINT
  new_render_pass_index_list.clear();
  new_render_pass_index_list.insert_or_assign(1, 0);
  dst_list = GetRenderCommandQueueTypeListWithNewPassIndex(1, new_render_pass_index_list, src_list, &memory_resource_work, &memory_resource_work);
  CHECK(dst_list.size() == 1); // NOLINT
  CHECK(dst_list[0] == CommandQueueType::kCompute); // NOLINT
}
TEST_CASE("RemoveDisabledCommandQueueType") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_set<CommandQueueType> disabled_command_queue_type_list{&memory_resource_work};
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
  SUBCASE("use all queues") {
    render_pass_command_queue_type_list = RemoveDisabledCommandQueueType(disabled_command_queue_type_list, std::move(render_pass_command_queue_type_list));
    CHECK(render_pass_command_queue_type_list.size() == 3); // NOLINT
    CHECK(render_pass_command_queue_type_list[0] == CommandQueueType::kGraphics); // NOLINT
    CHECK(render_pass_command_queue_type_list[1] == CommandQueueType::kCompute); // NOLINT
    CHECK(render_pass_command_queue_type_list[2] == CommandQueueType::kTransfer); // NOLINT
  }
  SUBCASE("disable compute queue") {
    disabled_command_queue_type_list.insert(CommandQueueType::kCompute);
    render_pass_command_queue_type_list = RemoveDisabledCommandQueueType(disabled_command_queue_type_list, std::move(render_pass_command_queue_type_list));
    CHECK(render_pass_command_queue_type_list.size() == 3); // NOLINT
    CHECK(render_pass_command_queue_type_list[0] == CommandQueueType::kGraphics); // NOLINT
    CHECK(render_pass_command_queue_type_list[1] == CommandQueueType::kGraphics); // NOLINT
    CHECK(render_pass_command_queue_type_list[2] == CommandQueueType::kTransfer); // NOLINT
  }
  SUBCASE("disable compute queue and transfer queue") {
    disabled_command_queue_type_list.insert(CommandQueueType::kCompute);
    disabled_command_queue_type_list.insert(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list = RemoveDisabledCommandQueueType(disabled_command_queue_type_list, std::move(render_pass_command_queue_type_list));
    CHECK(render_pass_command_queue_type_list.size() == 3); // NOLINT
    CHECK(render_pass_command_queue_type_list[0] == CommandQueueType::kGraphics); // NOLINT
    CHECK(render_pass_command_queue_type_list[1] == CommandQueueType::kGraphics); // NOLINT
    CHECK(render_pass_command_queue_type_list[2] == CommandQueueType::kGraphics); // NOLINT
  }
}
TEST_CASE("async compute") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  uint32_t pass_num = 5;
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  vector<unordered_set<CommandQueueType>> preceding_command_queue_type_list{&memory_resource_work};
  vector<unordered_set<uint32_t>> async_compute_group{&memory_resource_work};
  SUBCASE("intra-frame 1") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(1);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("intra-frame 2") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("intra-frame 3") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(1);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("intra-frame 4") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(3);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 1);
    CHECK(render_pass_order_succeeding_frame[1] == 0);
    CHECK(render_pass_order_succeeding_frame[2] == 3);
    CHECK(render_pass_order_succeeding_frame[3] == 2);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(!pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("intra-frame 5") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(1);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame[2] == 1);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
  }
  SUBCASE("intra-frame 6") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame[2] == 1);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("intra-frame 7") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(1);
    async_compute_group.back().insert(2);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame[2] == 1);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
  }
  SUBCASE("intra-frame 8") {
    pass_num = 15;
    render_pass_command_queue_type_list.clear();
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    async_compute_group.back().insert(5);
    async_compute_group.back().insert(6);
    async_compute_group.back().insert(7);
    async_compute_group.back().insert(8);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(9);
    async_compute_group.back().insert(10);
    async_compute_group.back().insert(11);
    async_compute_group.back().insert(12);
    async_compute_group.back().insert(13);
    async_compute_group.back().insert(14);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    CHECK(render_pass_order_succeeding_frame[5] == 5);
    CHECK(render_pass_order_succeeding_frame[6] == 6);
    CHECK(render_pass_order_succeeding_frame[7] == 7);
    CHECK(render_pass_order_succeeding_frame[8] == 8);
    CHECK(render_pass_order_succeeding_frame[9] == 9);
    CHECK(render_pass_order_succeeding_frame[10] == 10);
    CHECK(render_pass_order_succeeding_frame[11] == 11);
    CHECK(render_pass_order_succeeding_frame[12] == 12);
    CHECK(render_pass_order_succeeding_frame[13] == 13);
    CHECK(render_pass_order_succeeding_frame[14] == 14);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(!pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.contains(5));
    CHECK(!pass_signal_info_succeeding_frame.contains(6));
    CHECK(!pass_signal_info_succeeding_frame.contains(7));
    CHECK(pass_signal_info_succeeding_frame.contains(8));
    CHECK(!pass_signal_info_succeeding_frame.contains(9));
    CHECK(!pass_signal_info_succeeding_frame.contains(10));
    CHECK(!pass_signal_info_succeeding_frame.contains(11));
    CHECK(!pass_signal_info_succeeding_frame.contains(12));
    CHECK(!pass_signal_info_succeeding_frame.contains(13));
    CHECK(pass_signal_info_succeeding_frame.contains(14));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(6));
    CHECK(pass_signal_info_succeeding_frame.at(5).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(5).contains(12));
    CHECK(pass_signal_info_succeeding_frame.at(8).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(8).contains(9));
    CHECK(pass_signal_info_succeeding_frame.at(14).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(14).contains(0));
  }
  SUBCASE("intra-frame 9") {
    pass_num = 12;
    render_pass_command_queue_type_list.clear();
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    async_compute_group.back().insert(5);
    async_compute_group.back().insert(6);
    async_compute_group.back().insert(7);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(8);
    async_compute_group.back().insert(9);
    async_compute_group.back().insert(10);
    async_compute_group.back().insert(11);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame.size() == 12);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    CHECK(render_pass_order_succeeding_frame[5] == 5);
    CHECK(render_pass_order_succeeding_frame[6] == 6);
    CHECK(render_pass_order_succeeding_frame[7] == 7);
    CHECK(render_pass_order_succeeding_frame[8] == 8);
    CHECK(render_pass_order_succeeding_frame[9] == 9);
    CHECK(render_pass_order_succeeding_frame[10] == 10);
    CHECK(render_pass_order_succeeding_frame[11] == 11);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 5);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.contains(5));
    CHECK(!pass_signal_info_succeeding_frame.contains(6));
    CHECK(!pass_signal_info_succeeding_frame.contains(7));
    CHECK(!pass_signal_info_succeeding_frame.contains(8));
    CHECK(pass_signal_info_succeeding_frame.contains(9));
    CHECK(!pass_signal_info_succeeding_frame.contains(10));
    CHECK(pass_signal_info_succeeding_frame.contains(11));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(10));
    CHECK(pass_signal_info_succeeding_frame.at(5).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(5).contains(8));
    CHECK(pass_signal_info_succeeding_frame.at(9).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(9).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(11).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(11).contains(0));
    preceding_command_queue_type_list.clear();
    async_compute_group.clear();
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    async_compute_group.back().insert(5);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(8);
    async_compute_group.back().insert(9);
    async_compute_group.back().insert(10);
    async_compute_group.back().insert(11);
    render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    std::tie(pre_group_pass, group_pass) = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    std::tie(render_pass_order_initial_frame, render_pass_order_succeeding_frame) = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame.size() == 12);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 6);
    CHECK(render_pass_order_succeeding_frame[3] == 7);
    CHECK(render_pass_order_succeeding_frame[4] == 2);
    CHECK(render_pass_order_succeeding_frame[5] == 3);
    CHECK(render_pass_order_succeeding_frame[6] == 4);
    CHECK(render_pass_order_succeeding_frame[7] == 5);
    CHECK(render_pass_order_succeeding_frame[8] == 8);
    CHECK(render_pass_order_succeeding_frame[9] == 9);
    CHECK(render_pass_order_succeeding_frame[10] == 10);
    CHECK(render_pass_order_succeeding_frame[11] == 11);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    std::tie(pass_signal_info_initial_frame, unprocessed_signal_pass) = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 5);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.contains(5));
    CHECK(!pass_signal_info_succeeding_frame.contains(6));
    CHECK(pass_signal_info_succeeding_frame.contains(7));
    CHECK(!pass_signal_info_succeeding_frame.contains(8));
    CHECK(pass_signal_info_succeeding_frame.contains(9));
    CHECK(!pass_signal_info_succeeding_frame.contains(10));
    CHECK(pass_signal_info_succeeding_frame.contains(11));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(10));
    CHECK(pass_signal_info_succeeding_frame.at(5).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(5).contains(8));
    CHECK(pass_signal_info_succeeding_frame.at(7).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(7).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(7).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(9).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(9).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(9).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(11).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(11).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(11).contains(2));
  }
  SUBCASE("inter-frame 1") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(1);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 2);
    CHECK(render_pass_order_initial_frame[0] == 0);
    CHECK(render_pass_order_initial_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(unprocessed_signal_pass.size() == 1);
    CHECK(unprocessed_signal_pass.at(CommandQueueType::kGraphics) == 2);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(2).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(2).contains(1));
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
    preceding_command_queue_type_list.back().insert(CommandQueueType::kCompute);
    std::tie(pre_group_pass, group_pass) = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    std::tie(render_pass_order_initial_frame, render_pass_order_succeeding_frame) = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 3);
    CHECK(render_pass_order_initial_frame[0] == 0);
    CHECK(render_pass_order_initial_frame[1] == 2);
    CHECK(render_pass_order_initial_frame[2] == 3);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    std::tie(pass_signal_info_initial_frame, unprocessed_signal_pass) = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 3);
    CHECK(pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(pass_signal_info_initial_frame.contains(2));
    CHECK(pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(0).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(0).contains(3));
    CHECK(pass_signal_info_initial_frame.at(2).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(2).contains(1));
    CHECK(pass_signal_info_initial_frame.at(3).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("inter-frame 2") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 1);
    CHECK(render_pass_order_initial_frame[0] == 2);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(2).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(2).contains(1));
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("inter-frame 3") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(1);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 1);
    CHECK(render_pass_order_initial_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(0).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(0).contains(1));
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("inter-frame 4") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(3);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 1);
    CHECK(render_pass_order_initial_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 1);
    CHECK(render_pass_order_succeeding_frame[1] == 0);
    CHECK(render_pass_order_succeeding_frame[2] == 3);
    CHECK(render_pass_order_succeeding_frame[3] == 2);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(0).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(0).contains(1));
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(!pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("inter-frame 4-2") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kCompute);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(3);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 1);
    CHECK(render_pass_order_initial_frame[0] == 3);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 1);
    CHECK(render_pass_order_succeeding_frame[1] == 0);
    CHECK(render_pass_order_succeeding_frame[2] == 3);
    CHECK(render_pass_order_succeeding_frame[3] == 2);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(!pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("inter-frame 5") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(1);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 1);
    CHECK(render_pass_order_initial_frame[0] == 2);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame[2] == 1);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
  }
  SUBCASE("inter-frame 5-2") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kCompute);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(1);
    async_compute_group.back().insert(2);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 1);
    CHECK(render_pass_order_initial_frame[0] == 1);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame[2] == 1);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(1).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(1).contains(0));
    CHECK(pass_signal_info_succeeding_frame.size() == 2);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
  }
  SUBCASE("inter-frame 6") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 1);
    CHECK(render_pass_order_initial_frame[0] == 4);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame[2] == 1);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
  SUBCASE("inter-frame 7") {
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(1);
    async_compute_group.back().insert(2);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kCompute);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 2);
    CHECK(render_pass_order_initial_frame[0] == 2);
    CHECK(render_pass_order_initial_frame[1] == 3);
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 2);
    CHECK(render_pass_order_succeeding_frame[2] == 1);
    CHECK(render_pass_order_succeeding_frame[3] == 4);
    CHECK(render_pass_order_succeeding_frame[4] == 3);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 2);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(pass_signal_info_initial_frame.contains(2));
    CHECK(pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(pass_signal_info_initial_frame.at(2).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(2).contains(3));
    CHECK(pass_signal_info_initial_frame.at(3).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
  }
  SUBCASE("inter-frame 8") {
    pass_num = 15;
    render_pass_command_queue_type_list.clear();
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kCompute);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    async_compute_group.back().insert(5);
    async_compute_group.back().insert(6);
    async_compute_group.back().insert(7);
    async_compute_group.back().insert(8);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(9);
    async_compute_group.back().insert(10);
    async_compute_group.back().insert(11);
    async_compute_group.back().insert(12);
    async_compute_group.back().insert(13);
    async_compute_group.back().insert(14);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 6);
    CHECK(render_pass_order_initial_frame[0] == 6);
    CHECK(render_pass_order_initial_frame[1] == 7);
    CHECK(render_pass_order_initial_frame[2] == 8);
    CHECK(render_pass_order_initial_frame[3] == 9);
    CHECK(render_pass_order_initial_frame[4] == 10);
    CHECK(render_pass_order_initial_frame[5] == 11);
    CHECK(render_pass_order_succeeding_frame.size() == 15);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    CHECK(render_pass_order_succeeding_frame[5] == 5);
    CHECK(render_pass_order_succeeding_frame[6] == 6);
    CHECK(render_pass_order_succeeding_frame[7] == 7);
    CHECK(render_pass_order_succeeding_frame[8] == 8);
    CHECK(render_pass_order_succeeding_frame[9] == 9);
    CHECK(render_pass_order_succeeding_frame[10] == 10);
    CHECK(render_pass_order_succeeding_frame[11] == 11);
    CHECK(render_pass_order_succeeding_frame[12] == 12);
    CHECK(render_pass_order_succeeding_frame[13] == 13);
    CHECK(render_pass_order_succeeding_frame[14] == 14);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(!pass_signal_info_initial_frame.contains(5));
    CHECK(!pass_signal_info_initial_frame.contains(6));
    CHECK(!pass_signal_info_initial_frame.contains(7));
    CHECK(pass_signal_info_initial_frame.contains(8));
    CHECK(!pass_signal_info_initial_frame.contains(9));
    CHECK(!pass_signal_info_initial_frame.contains(10));
    CHECK(!pass_signal_info_initial_frame.contains(11));
    CHECK(!pass_signal_info_initial_frame.contains(12));
    CHECK(!pass_signal_info_initial_frame.contains(13));
    CHECK(!pass_signal_info_initial_frame.contains(14));
    CHECK(pass_signal_info_initial_frame.at(8).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(8).contains(9));
    CHECK(pass_signal_info_succeeding_frame.size() == 4);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(!pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.contains(5));
    CHECK(!pass_signal_info_succeeding_frame.contains(6));
    CHECK(!pass_signal_info_succeeding_frame.contains(7));
    CHECK(pass_signal_info_succeeding_frame.contains(8));
    CHECK(!pass_signal_info_succeeding_frame.contains(9));
    CHECK(!pass_signal_info_succeeding_frame.contains(10));
    CHECK(!pass_signal_info_succeeding_frame.contains(11));
    CHECK(!pass_signal_info_succeeding_frame.contains(12));
    CHECK(!pass_signal_info_succeeding_frame.contains(13));
    CHECK(pass_signal_info_succeeding_frame.contains(14));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(6));
    CHECK(pass_signal_info_succeeding_frame.at(5).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(5).contains(12));
    CHECK(pass_signal_info_succeeding_frame.at(8).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(8).contains(9));
    CHECK(pass_signal_info_succeeding_frame.at(14).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(14).contains(0));
  }
  SUBCASE("inter-frame 9") {
    pass_num = 12;
    render_pass_command_queue_type_list.clear();
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kTransfer);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(0);
    async_compute_group.back().insert(1);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    async_compute_group.back().insert(5);
    async_compute_group.back().insert(6);
    async_compute_group.back().insert(7);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(8);
    async_compute_group.back().insert(9);
    async_compute_group.back().insert(10);
    async_compute_group.back().insert(11);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 6);
    CHECK(render_pass_order_initial_frame[0] == 0);
    CHECK(render_pass_order_initial_frame[1] == 1);
    CHECK(render_pass_order_initial_frame[2] == 2);
    CHECK(render_pass_order_initial_frame[3] == 3);
    CHECK(render_pass_order_initial_frame[4] == 8);
    CHECK(render_pass_order_initial_frame[5] == 9);
    CHECK(render_pass_order_succeeding_frame.size() == 12);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    CHECK(render_pass_order_succeeding_frame[5] == 5);
    CHECK(render_pass_order_succeeding_frame[6] == 6);
    CHECK(render_pass_order_succeeding_frame[7] == 7);
    CHECK(render_pass_order_succeeding_frame[8] == 8);
    CHECK(render_pass_order_succeeding_frame[9] == 9);
    CHECK(render_pass_order_succeeding_frame[10] == 10);
    CHECK(render_pass_order_succeeding_frame[11] == 11);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 2);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(!pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(!pass_signal_info_initial_frame.contains(5));
    CHECK(!pass_signal_info_initial_frame.contains(6));
    CHECK(!pass_signal_info_initial_frame.contains(7));
    CHECK(!pass_signal_info_initial_frame.contains(8));
    CHECK(pass_signal_info_initial_frame.contains(9));
    CHECK(!pass_signal_info_initial_frame.contains(10));
    CHECK(!pass_signal_info_initial_frame.contains(11));
    CHECK(pass_signal_info_initial_frame.contains(1));
    CHECK(pass_signal_info_initial_frame.at(1).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(1).contains(2));
    CHECK(pass_signal_info_initial_frame.contains(9));
    CHECK(pass_signal_info_initial_frame.at(9).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(9).contains(0));
    CHECK(pass_signal_info_succeeding_frame.size() == 5);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.contains(5));
    CHECK(!pass_signal_info_succeeding_frame.contains(6));
    CHECK(!pass_signal_info_succeeding_frame.contains(7));
    CHECK(!pass_signal_info_succeeding_frame.contains(8));
    CHECK(pass_signal_info_succeeding_frame.contains(9));
    CHECK(!pass_signal_info_succeeding_frame.contains(10));
    CHECK(pass_signal_info_succeeding_frame.contains(11));
    CHECK(!pass_signal_info_succeeding_frame.contains(12));
    CHECK(!pass_signal_info_succeeding_frame.contains(13));
    CHECK(!pass_signal_info_succeeding_frame.contains(14));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(10));
    CHECK(pass_signal_info_succeeding_frame.at(5).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(5).contains(8));
    CHECK(pass_signal_info_succeeding_frame.at(9).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(9).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(11).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(11).contains(0));
  }
  SUBCASE("intra-frame + inter-frame") {
    pass_num = 12;
    render_pass_command_queue_type_list.clear();
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    preceding_command_queue_type_list.back().insert(CommandQueueType::kGraphics);
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(2);
    async_compute_group.back().insert(3);
    async_compute_group.back().insert(4);
    async_compute_group.back().insert(5);
    async_compute_group.back().insert(6);
    async_compute_group.back().insert(7);
    preceding_command_queue_type_list.push_back(unordered_set<CommandQueueType>{&memory_resource_work});
    async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
    async_compute_group.back().insert(8);
    async_compute_group.back().insert(9);
    async_compute_group.back().insert(10);
    async_compute_group.back().insert(11);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.size() == 2);
    CHECK(render_pass_order_initial_frame[0] == 2);
    CHECK(render_pass_order_initial_frame[1] == 3);
    CHECK(render_pass_order_succeeding_frame.size() == 12);
    CHECK(render_pass_order_succeeding_frame[0] == 0);
    CHECK(render_pass_order_succeeding_frame[1] == 1);
    CHECK(render_pass_order_succeeding_frame[2] == 2);
    CHECK(render_pass_order_succeeding_frame[3] == 3);
    CHECK(render_pass_order_succeeding_frame[4] == 4);
    CHECK(render_pass_order_succeeding_frame[5] == 5);
    CHECK(render_pass_order_succeeding_frame[6] == 6);
    CHECK(render_pass_order_succeeding_frame[7] == 7);
    CHECK(render_pass_order_succeeding_frame[8] == 8);
    CHECK(render_pass_order_succeeding_frame[9] == 9);
    CHECK(render_pass_order_succeeding_frame[10] == 10);
    CHECK(render_pass_order_succeeding_frame[11] == 11);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.size() == 1);
    CHECK(!pass_signal_info_initial_frame.contains(0));
    CHECK(!pass_signal_info_initial_frame.contains(1));
    CHECK(!pass_signal_info_initial_frame.contains(2));
    CHECK(pass_signal_info_initial_frame.contains(3));
    CHECK(!pass_signal_info_initial_frame.contains(4));
    CHECK(!pass_signal_info_initial_frame.contains(5));
    CHECK(!pass_signal_info_initial_frame.contains(6));
    CHECK(!pass_signal_info_initial_frame.contains(7));
    CHECK(!pass_signal_info_initial_frame.contains(8));
    CHECK(!pass_signal_info_initial_frame.contains(9));
    CHECK(!pass_signal_info_initial_frame.contains(10));
    CHECK(!pass_signal_info_initial_frame.contains(11));
    CHECK(pass_signal_info_initial_frame.contains(3));
    CHECK(pass_signal_info_initial_frame.at(3).size() == 1);
    CHECK(pass_signal_info_initial_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.size() == 5);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(!pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(!pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.contains(5));
    CHECK(!pass_signal_info_succeeding_frame.contains(6));
    CHECK(!pass_signal_info_succeeding_frame.contains(7));
    CHECK(!pass_signal_info_succeeding_frame.contains(8));
    CHECK(pass_signal_info_succeeding_frame.contains(9));
    CHECK(!pass_signal_info_succeeding_frame.contains(10));
    CHECK(pass_signal_info_succeeding_frame.contains(11));
    CHECK(!pass_signal_info_succeeding_frame.contains(12));
    CHECK(!pass_signal_info_succeeding_frame.contains(13));
    CHECK(!pass_signal_info_succeeding_frame.contains(14));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(10));
    CHECK(pass_signal_info_succeeding_frame.at(5).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(5).contains(8));
    CHECK(pass_signal_info_succeeding_frame.at(9).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(9).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(11).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(11).contains(0));
  }
  SUBCASE("no async compute group") {
    render_pass_command_queue_type_list.clear();
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
    render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
    auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
    auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, {}, &memory_resource_work);
    auto used_command_queues = GetUsedCommandQueueList(pre_group_pass[0], &memory_resource_work);
    auto [render_pass_order_initial_frame, render_pass_order_succeeding_frame] = ConfigureAsyncComputeGroupRenderPassOrder(used_command_queues, pre_group_pass, group_pass, preceding_command_queue_type_list, &memory_resource_work);
    CHECK(render_pass_order_initial_frame.empty());
    CHECK(render_pass_order_succeeding_frame.size() == 5);
    auto pass_signal_info_succeeding_frame = ConfigureAsyncComputeGroupPassSignals(pre_group_pass, group_pass, &memory_resource_work, &memory_resource_work);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    auto [pass_signal_info_initial_frame, unprocessed_signal_pass] = ConfigurePassSignalsForPrecedingFrame(group_pass, preceding_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    CHECK(unprocessed_signal_pass.empty());
    pass_signal_info_initial_frame = ConfigurePassSignalsFromPrecedingFrameToSuccedingFrame(render_pass_order_per_command_queue_type, pre_group_pass, group_pass, unprocessed_signal_pass, std::move(pass_signal_info_initial_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 3);
    CHECK(!pass_signal_info_succeeding_frame.contains(0));
    CHECK(!pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(1));
  }
}
TEST_CASE("GetAsyncGroupInfo") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<StrId, uint32_t> render_pass_id_map{&memory_resource_work};
  render_pass_id_map.insert_or_assign(StrId("pass_a"), 0);
  render_pass_id_map.insert_or_assign(StrId("pass_b"), 1);
  render_pass_id_map.insert_or_assign(StrId("pass_c"), 2);
  render_pass_id_map.insert_or_assign(StrId("pass_d"), 3);
  render_pass_id_map.insert_or_assign(StrId("pass_e"), 4);
  render_pass_id_map.insert_or_assign(StrId("pass_f"), 5);
  render_pass_id_map.insert_or_assign(StrId("pass_g"), 6);
  unordered_map<StrId, unordered_set<StrId>> async_compute_group_with_name{&memory_resource_work};
  async_compute_group_with_name.insert_or_assign(StrId("group_a"), unordered_set<StrId>{&memory_resource_work});
  async_compute_group_with_name.at(StrId("group_a")).insert(StrId("pass_a"));
  async_compute_group_with_name.at(StrId("group_a")).insert(StrId("pass_b"));
  async_compute_group_with_name.at(StrId("group_a")).insert(StrId("pass_c"));
  async_compute_group_with_name.insert_or_assign(StrId("group_b"), unordered_set<StrId>{&memory_resource_work});
  async_compute_group_with_name.at(StrId("group_b")).insert(StrId("pass_d"));
  async_compute_group_with_name.at(StrId("group_b")).insert(StrId("pass_e"));
  async_compute_group_with_name.insert_or_assign(StrId("group_c"), unordered_set<StrId>{&memory_resource_work});
  async_compute_group_with_name.at(StrId("group_c")).insert(StrId("pass_f"));
  async_compute_group_with_name.at(StrId("group_c")).insert(StrId("pass_g"));
  unordered_map<StrId, unordered_set<CommandQueueType>> preceding_command_queue_type_list_with_name{&memory_resource_work};
  preceding_command_queue_type_list_with_name.insert_or_assign(StrId("group_b"), unordered_set<CommandQueueType>{&memory_resource_work});
  preceding_command_queue_type_list_with_name.at(StrId("group_b")).insert(CommandQueueType::kCompute);
  auto [async_compute_group, preceding_command_queue_type_list] = GetAsyncGroupInfo(render_pass_id_map, async_compute_group_with_name, preceding_command_queue_type_list_with_name, &memory_resource_work, &memory_resource_work);
  CHECK(async_compute_group.size() == 3);
  CHECK(async_compute_group[0].size() == 3);
  CHECK(async_compute_group[0].contains(0));
  CHECK(async_compute_group[0].contains(1));
  CHECK(async_compute_group[0].contains(2));
  CHECK(async_compute_group[1].size() == 2);
  CHECK(async_compute_group[1].contains(3));
  CHECK(async_compute_group[1].contains(4));
  CHECK(async_compute_group[2].size() == 2);
  CHECK(async_compute_group[2].contains(5));
  CHECK(async_compute_group[2].contains(6));
  CHECK(preceding_command_queue_type_list.size() == 3);
  CHECK(preceding_command_queue_type_list[0].empty());
  CHECK(preceding_command_queue_type_list[1].size() == 1);
  CHECK(preceding_command_queue_type_list[1].contains(CommandQueueType::kCompute));
  CHECK(preceding_command_queue_type_list[2].empty());
}
TEST_CASE("ConfigureAsyncComputeGroupPerQueueType") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  unordered_map<CommandQueueType, vector<uint32_t>> render_pass_order_per_command_queue_type{&memory_resource_work};
  render_pass_order_per_command_queue_type.insert_or_assign(CommandQueueType::kGraphics, vector<uint32_t>{&memory_resource_work});
  render_pass_order_per_command_queue_type.at(CommandQueueType::kGraphics).push_back(0);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kGraphics).push_back(1);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kGraphics).push_back(4);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kGraphics).push_back(5);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kGraphics).push_back(8);
  render_pass_order_per_command_queue_type.insert_or_assign(CommandQueueType::kCompute,  vector<uint32_t>{&memory_resource_work});
  render_pass_order_per_command_queue_type.at(CommandQueueType::kCompute).push_back(2);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kCompute).push_back(3);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kCompute).push_back(6);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kCompute).push_back(7);
  render_pass_order_per_command_queue_type.at(CommandQueueType::kCompute).push_back(9);
  vector<unordered_set<uint32_t>> async_compute_group{&memory_resource_work};
  async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
  async_compute_group.back().insert(1);
  async_compute_group.back().insert(2);
  async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
  async_compute_group.back().insert(4);
  async_compute_group.back().insert(6);
  async_compute_group.back().insert(7);
  async_compute_group.push_back(unordered_set<uint32_t>{&memory_resource_work});
  async_compute_group.back().insert(5);
  async_compute_group.back().insert(9);
  auto [pre_group_pass, group_pass] = ConfigureAsyncComputeGroupPerQueueType(render_pass_order_per_command_queue_type, async_compute_group, &memory_resource_work);
  CHECK(pre_group_pass.size() == 4);
  CHECK(group_pass.size() == 3);
  CHECK(pre_group_pass[0].size() == 2);
  CHECK(pre_group_pass[1].size() == 2);
  CHECK(pre_group_pass[2].size() == 2);
  CHECK(pre_group_pass[3].size() == 2);
  CHECK(group_pass[0].size() == 2);
  CHECK(group_pass[1].size() == 2);
  CHECK(group_pass[2].size() == 2);
  CHECK(pre_group_pass[0].at(CommandQueueType::kGraphics).size() == 1);
  CHECK(pre_group_pass[0].at(CommandQueueType::kGraphics)[0] == 0);
  CHECK(group_pass[0].at(CommandQueueType::kGraphics).size() == 1);
  CHECK(group_pass[0].at(CommandQueueType::kGraphics)[0] == 1);
  CHECK(pre_group_pass[1].at(CommandQueueType::kGraphics).empty());
  CHECK(group_pass[1].at(CommandQueueType::kGraphics).size() == 1);
  CHECK(group_pass[1].at(CommandQueueType::kGraphics)[0] == 4);
  CHECK(pre_group_pass[2].at(CommandQueueType::kGraphics).empty());
  CHECK(group_pass[2].at(CommandQueueType::kGraphics).size() == 1);
  CHECK(group_pass[2].at(CommandQueueType::kGraphics)[0] == 5);
  CHECK(pre_group_pass[3].at(CommandQueueType::kGraphics).size() == 1);
  CHECK(pre_group_pass[3].at(CommandQueueType::kGraphics)[0] == 8);
  CHECK(pre_group_pass[0].at(CommandQueueType::kCompute).empty());
  CHECK(group_pass[0].at(CommandQueueType::kCompute).size() == 1);
  CHECK(group_pass[0].at(CommandQueueType::kCompute)[0] == 2);
  CHECK(pre_group_pass[1].at(CommandQueueType::kCompute).size() == 1);
  CHECK(pre_group_pass[1].at(CommandQueueType::kCompute)[0] == 3);
  CHECK(group_pass[1].at(CommandQueueType::kCompute).size() == 2);
  CHECK(group_pass[1].at(CommandQueueType::kCompute)[0] == 6);
  CHECK(group_pass[1].at(CommandQueueType::kCompute)[1] == 7);
  CHECK(pre_group_pass[2].at(CommandQueueType::kCompute).empty());
  CHECK(group_pass[2].at(CommandQueueType::kCompute).size() == 1);
  CHECK(group_pass[2].at(CommandQueueType::kCompute)[0] == 9);
  CHECK(pre_group_pass[3].at(CommandQueueType::kCompute).empty());
}
TEST_CASE("ConfigureFrameSyncSignalInfo") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  uint32_t pass_num = 5;
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kTransfer);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  auto render_pass_order_per_command_queue_type = GetRenderPassOrderPerQueue(pass_num, render_pass_command_queue_type_list, &memory_resource_work);
  unordered_map<uint32_t, unordered_set<uint32_t>> pass_signal_info_initial_frame{&memory_resource_work};
  unordered_map<uint32_t, unordered_set<uint32_t>> pass_signal_info_succeeding_frame{&memory_resource_work};
  pass_signal_info_succeeding_frame.insert_or_assign(0, unordered_set<uint32_t>{&memory_resource_work});
  pass_signal_info_succeeding_frame.at(0).insert(4);
  pass_signal_info_succeeding_frame.insert_or_assign(1, unordered_set<uint32_t>{&memory_resource_work});
  pass_signal_info_succeeding_frame.at(1).insert(3);
  SUBCASE("transfer in first group") {
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 5);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(2));
  }
  SUBCASE("transfer in latter group") {
    pass_signal_info_succeeding_frame.at(0).insert(2);
    pass_signal_info_succeeding_frame.at(1).insert(2);
    pass_signal_info_succeeding_frame = ConfigureFrameSyncSignalInfo(render_pass_order_per_command_queue_type, std::move(pass_signal_info_succeeding_frame), &memory_resource_work, &memory_resource_work);
    CHECK(pass_signal_info_initial_frame.empty());
    CHECK(pass_signal_info_succeeding_frame.size() == 5);
    CHECK(pass_signal_info_succeeding_frame.contains(0));
    CHECK(pass_signal_info_succeeding_frame.contains(1));
    CHECK(pass_signal_info_succeeding_frame.contains(2));
    CHECK(pass_signal_info_succeeding_frame.contains(3));
    CHECK(pass_signal_info_succeeding_frame.contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(4));
    CHECK(pass_signal_info_succeeding_frame.at(0).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(1).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(3));
    CHECK(pass_signal_info_succeeding_frame.at(1).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(2).size() == 2);
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(0));
    CHECK(pass_signal_info_succeeding_frame.at(2).contains(1));
    CHECK(pass_signal_info_succeeding_frame.at(3).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(3).contains(1));
    CHECK(!pass_signal_info_succeeding_frame.at(3).contains(2));
    CHECK(pass_signal_info_succeeding_frame.at(4).size() == 1);
    CHECK(pass_signal_info_succeeding_frame.at(4).contains(0));
    CHECK(!pass_signal_info_succeeding_frame.at(4).contains(2));
  }
}
TEST_CASE("DisposeProcessedFrameIndex") { // NOLINT
  using namespace illuminate; // NOLINT
  using namespace illuminate::core; // NOLINT
  using namespace illuminate::gfx; // NOLINT
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<uint32_t> next_frame_index_list{&memory_resource_work};
  next_frame_index_list.push_back(1);
  next_frame_index_list.push_back(2);
  next_frame_index_list.push_back(1);
  uint32_t next_frame_index = 0;
  std::tie(next_frame_index, next_frame_index_list) = DisposeProcessedFrameIndex(0, std::move(next_frame_index_list));
  CHECK(next_frame_index == 0);
  CHECK(next_frame_index_list.size() == 2);
  CHECK(next_frame_index_list[0] == 1);
  CHECK(next_frame_index_list[1] == 0);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
// TODO add option to remove memory alias barrier when in different batch (ExecuteCommandLists)
