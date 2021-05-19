#include "render_graph.h"
namespace illuminate::gfx {
constexpr bool IsBufferStateFlagMergeAcceptable(const BufferStateFlags& state) {
  if (state & kBufferStateFlagUav) return false;
  if (state & kBufferStateFlagRtv) return false;
  if (state & kBufferStateFlagDsvWrite) return false;
  if (state & kBufferStateFlagCopyDst) return false;
  if (state & kBufferStateFlagPresent) return false;
  return true;
}
constexpr bool IsBufferStateFlagsMergeable(const BufferStateFlags& a, const BufferStateFlags& b) {
  if (!IsBufferStateFlagMergeAcceptable(a)) return false;
  if (!IsBufferStateFlagMergeAcceptable(b)) return false;
  return true;
}
constexpr BufferStateFlags MergeBufferStateFlags(const BufferStateFlags& a, const BufferStateFlags& b) {
  return static_cast<BufferStateFlags>(a | b);
}
static BufferStateFlags MergeReadWriteFlag(const BufferStateFlags& state, const ReadWriteFlag& rw_flag) {
  BufferStateFlags ret = state;
  if (rw_flag & kReadFlag) ret = static_cast<BufferStateFlags>(ret | kBufferStateReadFlag);
  if (rw_flag & kWriteFlag) ret = static_cast<BufferStateFlags>(ret | kBufferStateWriteFlag);
  return ret;
}
static std::tuple<vector<BufferId>, vector<vector<BufferId>>, vector<vector<BufferStateFlags>>> InitBufferIdList(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, std::pmr::memory_resource* memory_resource) {
  vector<BufferId> buffer_id_list{memory_resource};
  vector<vector<BufferId>> render_pass_buffer_id_list{memory_resource};
  render_pass_buffer_id_list.reserve(render_pass_buffer_state_list.size());
  vector<vector<BufferStateFlags>> render_pass_buffer_state_flag_list{memory_resource};
  render_pass_buffer_state_flag_list.reserve(render_pass_buffer_state_list.size());
  unordered_map<StrId, BufferId> used_buffer_name{memory_resource};
  BufferId next_buffer_id = 0;
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& buffer_state_list = render_pass_buffer_state_list[pass_index];
    render_pass_buffer_id_list.push_back(vector<BufferId>{memory_resource});
    render_pass_buffer_id_list.back().reserve(buffer_state_list.size());
    render_pass_buffer_state_flag_list.push_back(vector<BufferStateFlags>{memory_resource});
    render_pass_buffer_state_flag_list.back().reserve(buffer_state_list.size());
    for (auto& buffer_state : buffer_state_list) {
      if (!used_buffer_name.contains(buffer_state.buffer_name) || (buffer_state.read_write_flag & kReadFlag) == 0) {
        used_buffer_name.insert_or_assign(buffer_state.buffer_name, next_buffer_id);
        buffer_id_list.push_back(next_buffer_id);
        next_buffer_id++;
      }
      render_pass_buffer_id_list.back().push_back(used_buffer_name.at(buffer_state.buffer_name));
      if ((buffer_state.state & kBufferStateFlagUav) || (buffer_state.state & kBufferStateFlagDsv)) {
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
static auto GetBufferNameIdMap(const uint32_t pass_num, const RenderPassBufferStateList& render_pass_buffer_state_list, const vector<vector<BufferId>>& render_pass_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<StrId, unordered_set<BufferId>> buffer_name_id_map{memory_resource};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto& current_pass_buffer_state_list = render_pass_buffer_state_list[pass_index];
    const auto buffer_num = static_cast<uint32_t>(current_pass_buffer_state_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      auto& name = current_pass_buffer_state_list[buffer_index].buffer_name;
      auto& id = render_pass_buffer_id_list[pass_index][buffer_index];
      if (!buffer_name_id_map.contains(name)) {
        buffer_name_id_map.insert_or_assign(name, unordered_set<BufferId>{memory_resource});
      }
      buffer_name_id_map.at(name).insert(id);
    }
  }
  return buffer_name_id_map;
}
static unordered_map<BufferId, BufferStateFlags> ConvertBufferNameToBufferIdForBufferStateFlagList(const unordered_map<StrId, unordered_set<BufferId>>& buffer_name_id_map, const unordered_map<StrId, BufferStateFlags>& state_flag_list_with_name, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, BufferStateFlags> state_flag_list{memory_resource};
  for (auto& [buffer_name, flag] : state_flag_list_with_name) {
    for (auto& buffer_id : buffer_name_id_map.at(buffer_name)) {
      state_flag_list.insert_or_assign(buffer_id, flag);
    }
  }
  return state_flag_list;
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
static unordered_map<uint32_t, unordered_map<uint32_t, int32_t>> CreateNodeDistanceMapInSameCommandQueueType(const uint32_t pass_num, const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_map<uint32_t, int32_t>> node_distance_map{memory_resource};
  unordered_map<CommandQueueType, uint32_t> last_pass_index_per_command_queue_type{memory_resource_work};
  last_pass_index_per_command_queue_type.reserve(kCommandQueueTypeNum);
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto [it, result] = node_distance_map.insert_or_assign(pass_index, unordered_map<uint32_t, int32_t>{memory_resource});
    auto& distance_map = it->second;
    distance_map.insert_or_assign(pass_index, 0);
    // insert info from previous pass in same queue type
    auto& command_queue_type = render_pass_command_queue_type_list[pass_index];
    if (last_pass_index_per_command_queue_type.contains(command_queue_type)) {
      auto& src_distance_map = node_distance_map.at(last_pass_index_per_command_queue_type.at(command_queue_type));
      distance_map.reserve(distance_map.size() + src_distance_map.size());
      for (auto& [src_index, src_distance] : src_distance_map) {
        distance_map.insert_or_assign(src_index, src_distance - 1);
      }
    }
    last_pass_index_per_command_queue_type.insert_or_assign(command_queue_type, pass_index);
  }
  return node_distance_map;
}
static unordered_map<uint32_t, unordered_map<uint32_t, int32_t>> AddNodeDistanceInReverseOrder(unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>&& node_distance_map) {
  for (auto& [root_index, distance_map] : node_distance_map) {
    for (auto& [pass_index, distance] : distance_map) {
      if (node_distance_map.at(pass_index).contains(root_index)) continue;
      node_distance_map.at(pass_index).insert_or_assign(root_index, -distance);
    }
  }
  return std::move(node_distance_map);
}
static unordered_map<uint32_t, unordered_map<uint32_t, int32_t>> AppendInterQueueNodeDistanceMap(const unordered_map<uint32_t, unordered_set<uint32_t>>& inter_queue_pass_dependency, unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>&& node_distance_map) {
  for (auto& [consumer_pass, producers] : inter_queue_pass_dependency) {
    auto&& distance_map = node_distance_map.at(consumer_pass);
    for (auto& producer : producers) {
      if (!distance_map.contains(producer)) {
        distance_map.insert_or_assign(producer, -1);
        for (auto& [descendant, distance] : distance_map) {
          if (distance <= 0) continue; // i.e. ancestor or self
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
  for (auto& [buffer_id, buffer_user_list] : buffer_user_pass_list) {
    auto user_pass_list_num = static_cast<uint32_t>(buffer_user_list.size());
    for (uint32_t user_pass_list_index = 0; user_pass_list_index < user_pass_list_num - 1; user_pass_list_index++) {
      if (buffer_user_list[user_pass_list_index].empty()) continue;
      src_pass_index_map.clear();
      dst_pass_index_map.clear();
      auto& src_user_pass_list = buffer_user_list[user_pass_list_index];
      for (uint32_t src_user_pass_list_index = 0; src_user_pass_list_index < src_user_pass_list.size(); src_user_pass_list_index++) {
        auto& src_pass_index = src_user_pass_list[src_user_pass_list_index];
        auto& src_command_queue_type = render_pass_command_queue_type_list[src_pass_index];
        if (src_pass_index_map.contains(src_command_queue_type) && src_pass_index_map.at(src_command_queue_type) > src_pass_index) continue;
        src_pass_index_map.insert_or_assign(src_command_queue_type, src_pass_index);
      }
      auto& dst_user_pass_list = buffer_user_list[user_pass_list_index + 1];
      for (uint32_t dst_user_pass_list_index = 0; dst_user_pass_list_index < dst_user_pass_list.size(); dst_user_pass_list_index++) {
        auto& dst_pass_index = dst_user_pass_list[dst_user_pass_list_index];
        auto& dst_command_queue_type = render_pass_command_queue_type_list[dst_pass_index];
        if (dst_pass_index_map.contains(dst_command_queue_type) && dst_pass_index_map.at(dst_command_queue_type) < dst_pass_index) continue;
        dst_pass_index_map.insert_or_assign(dst_command_queue_type, dst_pass_index);
      }
      for (auto& [src_command_queue_type, src_pass_index] : src_pass_index_map) {
        for (auto& [dst_command_queue_type, dst_pass_index] : dst_pass_index_map) {
          if (src_command_queue_type == dst_command_queue_type) continue;
          bool insert_new_pass = true;
          if (inter_queue_pass_dependency.contains(dst_pass_index)) {
            for (auto& pass_index : inter_queue_pass_dependency.at(dst_pass_index)) {
              if (render_pass_command_queue_type_list[pass_index] != dst_command_queue_type) continue;
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
static unordered_map<uint32_t, unordered_map<uint32_t, int32_t>> ConfigureTripleInterQueueDependency(const uint32_t pass_num, unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>&& node_distance_map) {
  for (uint32_t ancestor = 0; ancestor < pass_num; ancestor++) {
    auto& ancestor_map = node_distance_map.at(ancestor);
    for (auto& [middle_pass, distance_to_middle_pass] : ancestor_map) {
      if (distance_to_middle_pass <= 0) continue;
      auto& middle_map = node_distance_map.at(middle_pass);
      for (uint32_t descendant = middle_pass + 1; descendant < pass_num; descendant++) {
        if (ancestor_map.contains(descendant)) continue;
        if (!middle_map.contains(descendant)) continue;
        if (middle_map.at(descendant) <= 0) continue;
        ancestor_map.insert_or_assign(descendant, distance_to_middle_pass + middle_map.at(descendant));
      }
    }
  }
  return std::move(node_distance_map);
}
static unordered_map<uint32_t, unordered_set<uint32_t>> RemoveRedundantDependencyFromSameQueuePredecessors(const uint32_t pass_num, const vector<CommandQueueType>& render_pass_command_queue_type_list, unordered_map<uint32_t, unordered_set<uint32_t>>&& inter_queue_pass_dependency, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<CommandQueueType, unordered_map<CommandQueueType, uint32_t>> processed_pass_per_queue{memory_resource_work};
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    if (!inter_queue_pass_dependency.contains(pass_index)) continue;
    auto& command_queue_type = render_pass_command_queue_type_list[pass_index];
    if (!processed_pass_per_queue.contains(command_queue_type)) {
      processed_pass_per_queue.insert_or_assign(command_queue_type, unordered_map<CommandQueueType, uint32_t>{memory_resource_work});
    }
    auto& processed_pass_list = processed_pass_per_queue.at(command_queue_type);
    auto& dependant_pass_list = inter_queue_pass_dependency.at(pass_index);
    auto it = dependant_pass_list.begin();
    while (it != dependant_pass_list.end()) {
      auto& dependant_pass = *it;
      auto& dependant_pass_command_queue_type = render_pass_command_queue_type_list[dependant_pass];
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
static CommandQueueTypeFlags GetBufferStateValidCommandQueueTypeFlags(const BufferStateFlags state) {
  if (!(state & ~(kBufferStateFlagCopySrc | kBufferStateFlagCopyDst | kBufferStateFlagCommon))) return kCommandQueueTypeFlagsAll;
  if (state & kBufferStateFlagSrvPsOnly) return kCommandQueueTypeFlagsGraphics;
  if (state & kBufferStateFlagRtv) return kCommandQueueTypeFlagsGraphics;
  if (state & kBufferStateFlagDsv) return kCommandQueueTypeFlagsGraphics;
  return kCommandQueueTypeFlagsGraphicsCompute;
}
static uint32_t FindClosestCommonDescendant(const vector<uint32_t>& ancestors, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, const vector<CommandQueueTypeFlags>& render_pass_command_queue_type_list, const CommandQueueTypeFlags valid_queues) {
  if (ancestors.empty()) {
    // return pass without ancestor
    const uint32_t invalid_pass = ~0u;
    uint32_t retval = invalid_pass;
    for (auto& [pass_index, distance_map] : node_distance_map) {
      if (pass_index > retval) continue;
      if (!(render_pass_command_queue_type_list[pass_index] & valid_queues)) continue;
      if (std::find_if(distance_map.begin(), distance_map.end(), [](const auto& pair) { return pair.second < 0; }) == distance_map.end()) {
        retval = pass_index;
      }
    }
    if (retval != invalid_pass) {
      return retval;
    }
    for (uint32_t i = 0; i < render_pass_command_queue_type_list.size(); i++) {
      if (render_pass_command_queue_type_list[i] & valid_queues) return i;
    }
    logerror("valid pass not found in FindClosestCommonDescendant {}", valid_queues);
    return invalid_pass;
  }
  if (ancestors.size() == 1 && (render_pass_command_queue_type_list[ancestors[0]] & valid_queues)) return ancestors[0];
  // find closest pass to all ancestors
  auto& cand_map = node_distance_map.at(ancestors[0]);
  int32_t min_distance = std::numeric_limits<int>::max();
  uint32_t ret_pass = ~0u;
  for (auto& [cand_pass, distance] : cand_map) {
    if (distance > min_distance) continue;
    if (!(render_pass_command_queue_type_list[cand_pass] & valid_queues)) continue;
    bool valid = true;
    for (uint32_t i = 1; i < ancestors.size(); i++) {
      auto& current_distance_map = node_distance_map.at(ancestors[i]);
      if (!current_distance_map.contains(cand_pass) || current_distance_map.at(cand_pass) < 0) {
        valid = false;
        break;
      }
    }
    if (!valid) continue;
    ret_pass = cand_pass;
    min_distance = distance;
  }
  return ret_pass;
}
static uint32_t FindClosestCommonAncestor(const vector<uint32_t>& descendants, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, const vector<CommandQueueTypeFlags>& render_pass_command_queue_type_list, const CommandQueueTypeFlags valid_queues) {
  if (descendants.empty()) {
    // return pass without descendants
    const uint32_t invalid_pass = ~0u;
    uint32_t retval = invalid_pass;
    for (auto& [pass_index, distance_map] : node_distance_map) {
      if (pass_index > retval) continue;
      if (!(render_pass_command_queue_type_list[pass_index] & valid_queues)) continue;
      if (std::find_if(distance_map.begin(), distance_map.end(), [](const auto& pair) { return pair.second > 0; }) == distance_map.end()) {
        retval = pass_index;
      }
    }
    if (retval != invalid_pass) {
      return retval;
    }
    const auto len = static_cast<uint32_t>(render_pass_command_queue_type_list.size());
    for (uint32_t i = len - 1; i < len/*i is unsigned*/; i--) {
      if (render_pass_command_queue_type_list[i] & valid_queues) return i;
    }
    logerror("valid pass not found in FindClosestCommonAncestor {}", valid_queues);
    return invalid_pass;
  }
  if (descendants.size() == 1 && (render_pass_command_queue_type_list[descendants[0]] & valid_queues)) return descendants[0];
  // find closest pass to all descendants
  auto& cand_map = node_distance_map.at(descendants[0]);
  int32_t min_distance = std::numeric_limits<int>::min();
  uint32_t ret_pass = ~0u;
  for (auto& [cand_pass, distance] : cand_map) {
    if (distance > 0) continue;
    if (distance < min_distance) continue;
    if (!(render_pass_command_queue_type_list[cand_pass] & valid_queues)) continue;
    bool valid = true;
    for (uint32_t i = 1; i < descendants.size(); i++) {
      auto& current_distance_map = node_distance_map.at(descendants[i]);
      if (!current_distance_map.contains(cand_pass) || current_distance_map.at(cand_pass) > 0) {
        valid = false;
        break;
      }
    }
    if (!valid) continue;
    ret_pass = cand_pass;
    min_distance = distance;
  }
  return ret_pass;
}
constexpr CommandQueueTypeFlags ConvertCommandQueueTypeToFlag(const CommandQueueType& type) {
  switch (type) {
    case CommandQueueType::kGraphics: return kCommandQueueTypeFlagsGraphics;
    case CommandQueueType::kCompute:  return kCommandQueueTypeFlagsCompute;
    case CommandQueueType::kTransfer: return kCommandQueueTypeFlagsTransfer;
  }
  return kCommandQueueTypeFlagsGraphics;
}
static vector<CommandQueueTypeFlags> ConvertToCommandQueueTypeFlagsList(const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource) {
  auto num = static_cast<uint32_t>(render_pass_command_queue_type_list.size());
  vector<CommandQueueTypeFlags> retval{memory_resource};
  retval.resize(num);
  for (uint32_t i = 0; i < num; i++) {
    retval[i] = ConvertCommandQueueTypeToFlag(render_pass_command_queue_type_list[i]);
  }
  return retval;
}
static unordered_map<BufferId, vector<BufferStateChangeInfo>> CreateBufferStateChangeInfoList(const vector<CommandQueueTypeFlags>& render_pass_command_queue_type_flag_list, const unordered_map<uint32_t, unordered_map<uint32_t, int32_t>>& node_distance_map, const unordered_map<BufferId, vector<BufferStateFlags>>& buffer_state_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, vector<BufferStateChangeInfo>> buffer_state_change_info_list_map{memory_resource};
  for (auto& [buffer_id, current_buffer_state_list] : buffer_state_list) {
    buffer_state_change_info_list_map.insert_or_assign(buffer_id, vector<BufferStateChangeInfo>{memory_resource});
    auto& current_buffer_state_change_info_list = buffer_state_change_info_list_map.at(buffer_id);
    auto& current_buffer_user_pass_list = buffer_user_pass_list.at(buffer_id);
    const auto buffer_state_num = static_cast<uint32_t>(current_buffer_user_pass_list.size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_state_num - 1; buffer_index++) {
      auto& current_state = current_buffer_state_list[buffer_index];
      auto& next_state = current_buffer_state_list[buffer_index + 1];
      auto valid_command_queue_type_flag = static_cast<CommandQueueTypeFlags>(GetBufferStateValidCommandQueueTypeFlags(current_state) & GetBufferStateValidCommandQueueTypeFlags(next_state));
      auto& current_users = current_buffer_user_pass_list[buffer_index];
      auto& next_users = current_buffer_user_pass_list[buffer_index + 1];
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
      if (current_buffer_state_change_info.barrier_begin_pass_index + 1 != current_buffer_state_change_info.barrier_end_pass_index) continue;
      if (current_buffer_state_change_info.barrier_begin_pass_pos_type != BarrierPosType::kPostPass) continue;
      if (current_buffer_state_change_info.barrier_end_pass_pos_type != BarrierPosType::kPrePass) continue;
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
static unordered_map<uint32_t, BufferConfig> CreateBufferConfigList(const vector<BufferId>& buffer_id_list,
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
  for (auto& buffer_id : buffer_id_list) {
    auto [it, result] = buffer_config_list.insert_or_assign(buffer_id, BufferConfig{});
    auto& config = it->second;
    auto& buffer_name = buffer_id_name_map.at(buffer_id);
    if (buffer_size_info_list.contains(buffer_name)) {
      auto& info = buffer_size_info_list.at(buffer_name);
      config.width = GetPhysicalBufferSize(info.type, info.width, primary_buffer_width, swapchain_buffer_width);
      config.height = GetPhysicalBufferSize(info.type, info.height, primary_buffer_height, swapchain_buffer_height);
    } else {
      config.width = primary_buffer_width;
      config.height = primary_buffer_height;
    }
    {
      auto& current_buffer_state_list = buffer_state_list.at(buffer_id);
      config.initial_state_flags = current_buffer_state_list.front();
      for (auto& state : current_buffer_state_list) {
        config.state_flags = MergeBufferStateFlags(config.state_flags, state);
      }
    }
    if (buffer_default_clear_value_list.contains(buffer_name)) {
      config.clear_value = buffer_default_clear_value_list.at(buffer_name);
    } else {
      if (config.state_flags & kBufferStateFlagRtv) {
        config.clear_value = GetClearValueDefaultColorBuffer();
      } else if (config.state_flags & kBufferStateFlagDsv) {
        config.clear_value = GetClearValueDefaultDepthBuffer();
      }
    }
    config.dimension = buffer_dimension_type_list.contains(buffer_name) ? buffer_dimension_type_list.at(buffer_name) : BufferDimensionType::k2d;
    if (buffer_format_list.contains(buffer_name)) {
      config.format = buffer_format_list.at(buffer_name);
    } else {
      config.format = (config.state_flags & kBufferStateFlagDsv) ? BufferFormat::kD32Float : BufferFormat::kR8G8B8A8Unorm;
    }
    config.depth_stencil_flag = buffer_depth_stencil_flag_list.contains(buffer_name) ? buffer_depth_stencil_flag_list.at(buffer_name) : DepthStencilFlag::kDefault;
  }
  return buffer_config_list;
}
static std::tuple<vector<vector<BarrierConfig>>, vector<vector<BarrierConfig>>> ConfigureBarriers(const uint32_t render_pass_num, const vector<BufferId>& buffer_id_list, const unordered_map<BufferId, vector<BufferStateChangeInfo>>& buffer_state_change_info_list_map, std::pmr::memory_resource* memory_resource_barriers) {
  vector<vector<BarrierConfig>> barriers_pre_pass{memory_resource_barriers};
  barriers_pre_pass.resize(render_pass_num);
  vector<vector<BarrierConfig>> barriers_post_pass{memory_resource_barriers};
  barriers_post_pass.resize(render_pass_num);
  for (auto& buffer_id : buffer_id_list) {
    auto& buffer_state_change_info_list = buffer_state_change_info_list_map.at(buffer_id);
    for (auto& buffer_state_change_info : buffer_state_change_info_list) {
      BarrierConfig barrier{
        .buffer_id = buffer_id,
        .split_type = BarrierSplitType::kNone,
      };
      bool disable_split = false;
      if ((buffer_state_change_info.state_before & kBufferStateFlagUav) && (buffer_state_change_info.state_after & kBufferStateFlagUav)) {
        if ((buffer_state_change_info.state_before == kBufferStateFlagUavWrite && !(buffer_state_change_info.state_before & kBufferStateReadFlag) &&
             buffer_state_change_info.state_after == kBufferStateFlagUavRead && !(buffer_state_change_info.state_after & kBufferStateWriteFlag)) ||
            (buffer_state_change_info.state_before == kBufferStateFlagUavRead && !(buffer_state_change_info.state_before & kBufferStateWriteFlag) &&
             buffer_state_change_info.state_after == kBufferStateFlagUavWrite && !(buffer_state_change_info.state_after & kBufferStateReadFlag))) {
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
  return {barriers_pre_pass, barriers_post_pass};
}
static unordered_map<uint32_t, unordered_set<uint32_t>> ConfigureQueueSignals(const uint32_t pass_num, const unordered_map<uint32_t, unordered_set<uint32_t>>& inter_queue_pass_dependency, const vector<CommandQueueType>& render_pass_command_queue_type_list, std::pmr::memory_resource* memory_resource_signals, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_set<uint32_t>> queue_signal{memory_resource_signals};
  for (auto& [dst_pass_index, src_pass_list] : inter_queue_pass_dependency) {
    for (auto& src_pass_index : src_pass_list) {
      if (!queue_signal.contains(src_pass_index)) {
        queue_signal.insert_or_assign(src_pass_index, unordered_set<uint32_t>{memory_resource_signals});
      }
      queue_signal.at(src_pass_index).insert(dst_pass_index);
    }
  }
  unordered_map<CommandQueueType, uint32_t> first_pass_per_queue{memory_resource_work};
  unordered_map<CommandQueueType, uint32_t> last_pass_per_queue{memory_resource_work};
  for (uint32_t i = 0; i < pass_num; i++) {
    auto& command_queue_type = render_pass_command_queue_type_list[i];
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
  for (auto& [src_command_queue_type, src_pass] : last_pass_per_queue) {
    for (auto& [dst_command_queue_type, dst_pass] : first_pass_per_queue) {
      if (src_command_queue_type == dst_command_queue_type) continue;
      if (!queue_signal.contains(src_pass)) {
        queue_signal.insert_or_assign(src_pass, unordered_set<uint32_t>(memory_resource_signals));
      }
      queue_signal.at(src_pass).insert(dst_pass);
    }
  }
  return queue_signal;
}
constexpr bool IsMergeableForBufferCreationImpl(const BufferStateFlags& a, const BufferStateFlags& b) {
  if (a & kBufferStateFlagRtv) {
    if (b & kBufferStateFlagDsv) return false;
  }
  if (a & kBufferStateFlagDsv) {
    if (b & kBufferStateFlagCbvUpload) return false;
  }
  return true;
}
constexpr bool IsMergeableForBufferCreation(const BufferStateFlags& a, const BufferStateFlags& b) {
  if (a == b) return true;
  if (!IsMergeableForBufferCreationImpl(a, b)) return false;
  if (!IsMergeableForBufferCreationImpl(b, a)) return false;
  return true;
}
constexpr bool IsBufferConfigMergeable(const BufferConfig& a, const BufferConfig& b) {
  if (a.width != b.width) return false;
  if (a.height != b.height) return false;
  if (a.dimension != b.dimension) return false;
  if (a.format != b.format) return false;
  if (!IsMergeableForBufferCreation(a.state_flags, b.state_flags)) return false;
  return true;
}
static auto UpdateBufferIdListUsingReusableBuffers(const unordered_map<BufferId, BufferId>& reusable_buffers, vector<BufferId>&& buffer_id_list) {
  auto new_end = std::remove_if(buffer_id_list.begin(), buffer_id_list.end(), [&reusable_buffers](const auto& id) { return reusable_buffers.contains(id); });
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
static std::tuple<unordered_map<BufferId, vector<BufferStateFlags>>, unordered_map<BufferId, vector<vector<uint32_t>>>> UpdateBufferStateListAndUserPassListUsingReusableBuffers(const unordered_map<BufferId, BufferId>& reusable_buffers, unordered_map<BufferId, vector<BufferStateFlags>>&& buffer_state_list, unordered_map<BufferId, vector<vector<uint32_t>>>&& buffer_user_pass_list) {
  for (auto& [original_id, new_id] : reusable_buffers) {
    auto&& src_state = buffer_state_list.at(original_id);
    auto&& dst_state = buffer_state_list.at(new_id);
    auto&& src_user_pass = buffer_user_pass_list.at(original_id);
    auto&& dst_user_pass = buffer_user_pass_list.at(new_id);
    dst_state.insert(dst_state.end(), std::make_move_iterator(src_state.begin()), std::make_move_iterator(src_state.end()));
    dst_user_pass.insert(dst_user_pass.end(), std::make_move_iterator(src_user_pass.begin()), std::make_move_iterator(src_user_pass.end()));
    buffer_state_list.erase(original_id);
    buffer_user_pass_list.erase(original_id);
  }
  return {std::move(buffer_state_list), std::move(buffer_user_pass_list)};
}
static auto MergeReusedBufferConfigs(const unordered_map<BufferId, BufferId>& reusable_buffers, unordered_map<BufferId, BufferConfig>&& buffer_config_list) {
  for (auto& [original_id, new_id] : reusable_buffers) {
    auto& original_flag = buffer_config_list.at(original_id).state_flags;
    auto& new_flag = buffer_config_list.at(new_id).state_flags;
    new_flag = MergeBufferStateFlags(new_flag, original_flag);
    buffer_config_list.erase(original_id);
  }
  return std::move(buffer_config_list);
}
static auto CollectExternalBufferIds(const unordered_map<StrId, unordered_set<BufferId>>& buffer_name_id_map, const unordered_set<StrId>& external_buffer_names, std::pmr::memory_resource* memory_resource) {
  unordered_set<BufferId> external_buffer_id_list{memory_resource};
  external_buffer_id_list.reserve(external_buffer_names.size());
  for (auto& buffer_name : external_buffer_names) {
    for (auto& buffer_id : buffer_name_id_map.at(buffer_name)) {
      external_buffer_id_list.insert(buffer_id);
    }
  }
  return external_buffer_id_list;
}
static std::tuple<vector<vector<uint32_t>>, vector<vector<uint32_t>>> ConfigureRenderPassNewAndExpiredBuffers(const uint32_t pass_num, const vector<uint32_t>& buffer_id_list, const unordered_map<BufferId, vector<vector<uint32_t>>>& buffer_user_pass_list, std::pmr::memory_resource* memory_resource) {
  vector<vector<uint32_t>> render_pass_new_buffer_list{memory_resource};
  vector<vector<uint32_t>> render_pass_expired_buffer_list{memory_resource};
  render_pass_new_buffer_list.reserve(pass_num);
  render_pass_expired_buffer_list.reserve(pass_num);
  for (uint32_t i = 0; i < pass_num; i++) {
    render_pass_new_buffer_list.push_back(vector<uint32_t>{memory_resource});
    render_pass_expired_buffer_list.push_back(vector<uint32_t>{memory_resource});
  }
  for (auto& buffer_id : buffer_id_list) {
    auto& list_list = buffer_user_pass_list.at(buffer_id);
    uint32_t min = pass_num;
    uint32_t max = 0;
    for (auto& list : list_list) {
      for (auto& pass : list) {
        if (pass < min) min = pass;
        if (pass > max) max = pass;
      }
    }
    render_pass_new_buffer_list[min].push_back(buffer_id);
    render_pass_expired_buffer_list[max].push_back(buffer_id);
  }
  return {render_pass_new_buffer_list, render_pass_expired_buffer_list};
}
static constexpr auto AlignAddress(const uint32_t addr, const uint32_t align) {
  const auto mask = align - 1;
  return (addr + mask) & ~mask;
}
static uint32_t RetainMemory(const uint32_t size_in_bytes, const uint32_t alignment_in_bytes, vector<uint32_t>* used_range_start, vector<uint32_t>* used_range_end) {
  auto range_num = static_cast<uint32_t>(used_range_start->size());
  for (uint32_t range_index = 0; range_index < range_num; range_index++) {
    auto addr = (range_index > 0) ? AlignAddress((*used_range_end)[range_index - 1], alignment_in_bytes) : 0;
    if (addr + size_in_bytes > (*used_range_start)[range_index]) continue;
    used_range_start->insert(used_range_start->begin() + range_index, addr);
    used_range_end->insert(used_range_end->begin() + range_index, addr + size_in_bytes);
    return addr;
  }
  uint32_t addr = (range_num == 0) ? 0 : AlignAddress(used_range_end->back(), alignment_in_bytes);
  used_range_start->push_back(addr);
  used_range_end->push_back(addr + size_in_bytes);
  return addr;
}
static void RetainSpecificMemory(const uint32_t addr, const uint32_t size_in_bytes, vector<uint32_t>* used_range_start, vector<uint32_t>* used_range_end) {
  auto range_num = static_cast<uint32_t>(used_range_start->size());
  for (uint32_t range_index = 0; range_index < range_num; range_index++) {
    if ((*used_range_start)[range_index] < addr) continue;
    ASSERT((range_index < range_num - 1) ? (addr + size < used_range_start[range_index + 1]) : true);
    used_range_start->insert(used_range_start->begin() + range_index, addr);
    used_range_end->insert(used_range_end->begin() + range_index, addr + size_in_bytes);
    return;
  }
  used_range_start->push_back(addr);
  used_range_end->push_back(addr + size_in_bytes);
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
    if (start > start_addr) return;
  }
}
static std::tuple<unordered_map<BufferId, uint32_t>, unordered_map<BufferId, BufferId>, unordered_map<uint32_t, vector<BufferId>>, unordered_map<uint32_t, vector<BufferId>>> ConfigureBufferAddressOffset(const uint32_t pass_num, const vector<vector<BufferId>>& render_pass_buffer_id_list, const unordered_map<BufferId, unordered_map<BufferId, int32_t>>& node_distance_map, const vector<vector<uint32_t>>& render_pass_new_buffer_list, const vector<vector<uint32_t>>& render_pass_expired_buffer_list, const unordered_map<BufferId, BufferConfig>& buffer_config_list, const unordered_map<BufferId, uint32_t>& buffer_size_list, const unordered_map<BufferId, uint32_t>& buffer_alignment_list, const unordered_set<BufferId>& excluded_buffers, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<BufferId, uint32_t> buffer_address_offset_list{memory_resource};
  buffer_address_offset_list.reserve(buffer_config_list.size());
  unordered_map<BufferId, BufferId> renamed_buffers{memory_resource};
  unordered_map<uint32_t, vector<BufferId>> render_pass_before_memory_aliasing_list{memory_resource};
  unordered_map<uint32_t, vector<BufferId>> render_pass_after_memory_aliasing_list{memory_resource};
  vector<uint32_t> used_range_start{memory_resource_work};
  vector<uint32_t> used_range_end{memory_resource_work};
  for (auto& new_buffer : render_pass_new_buffer_list[0]) {
    if (excluded_buffers.contains(new_buffer)) continue;
    buffer_address_offset_list.insert_or_assign(new_buffer, RetainMemory(buffer_size_list.at(new_buffer), buffer_alignment_list.at(new_buffer), &used_range_start, &used_range_end));
  }
  unordered_set<BufferId> processed_new_buffer_list{memory_resource_work};
  vector<uint32_t> memory_reusable_buffer_list{memory_resource_work};
  for (uint32_t pass_index = 1; pass_index < pass_num; pass_index++) {
    // release expired buffers
    for (auto& expired_buffer : render_pass_expired_buffer_list[pass_index-1]) {
      if (excluded_buffers.contains(expired_buffer)) continue;
      ReturnMemory(buffer_address_offset_list.at(expired_buffer), &used_range_start, &used_range_end);
      memory_reusable_buffer_list.push_back(expired_buffer);
    }
    // find reusable buffers
    for (auto& new_buffer : render_pass_new_buffer_list[pass_index]) {
      if (excluded_buffers.contains(new_buffer)) continue;
      for (auto it = memory_reusable_buffer_list.begin(); it != memory_reusable_buffer_list.end(); it++) {
        auto& expired_buffer = *it;
        if (!IsBufferConfigMergeable(buffer_config_list.at(new_buffer), buffer_config_list.at(expired_buffer))) continue;
        auto& addr = buffer_address_offset_list.at(expired_buffer);
        RetainSpecificMemory(addr, buffer_size_list.at(new_buffer), &used_range_start, &used_range_end);
        buffer_address_offset_list.insert_or_assign(new_buffer, addr);
        renamed_buffers.insert_or_assign(new_buffer, expired_buffer);
        processed_new_buffer_list.insert(new_buffer);
        memory_reusable_buffer_list.erase(it);
        break;
      }
    }
    // allocate new buffers
    for (auto& new_buffer : render_pass_new_buffer_list[pass_index]) {
      if (excluded_buffers.contains(new_buffer)) continue;
      if (processed_new_buffer_list.contains(new_buffer)) continue;
      auto& buffer_size = buffer_size_list.at(new_buffer);
      auto addr = RetainMemory(buffer_size, buffer_alignment_list.at(new_buffer), &used_range_start, &used_range_end);
      buffer_address_offset_list.insert_or_assign(new_buffer, addr);
      // check for memory aliasing
      for (auto it = memory_reusable_buffer_list.begin(); it != memory_reusable_buffer_list.end(); it++) {
        auto& expired_buffer = *it;
        auto expired_region_start = buffer_address_offset_list.at(expired_buffer);
        auto expired_region_end = expired_region_start + buffer_alignment_list.at(expired_buffer);
        if (addr + buffer_size <= expired_region_start) continue;
        if (addr >= expired_region_end) continue;
        if (!render_pass_before_memory_aliasing_list.contains(pass_index-1)) {
          render_pass_before_memory_aliasing_list.insert_or_assign(pass_index-1, vector<BufferId>{memory_resource});
          render_pass_after_memory_aliasing_list.insert_or_assign(pass_index-1, vector<BufferId>{memory_resource});
        }
        render_pass_before_memory_aliasing_list.at(pass_index-1).push_back(expired_buffer);
        render_pass_after_memory_aliasing_list.at(pass_index-1).push_back(new_buffer);
        memory_reusable_buffer_list.erase(it);
        break;
      }
    }
    processed_new_buffer_list.clear();
  }
  return {buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list};
}
static std::tuple<unordered_map<BufferId, uint32_t>, unordered_map<BufferId, uint32_t>> GetBufferSizeInfo(const unordered_map<BufferId, BufferConfig>& buffer_config_list, const std::function<std::tuple<uint32_t, uint32_t>(const BufferConfig&)>& configure_function, std::pmr::memory_resource* memory_resource) {
  unordered_map<BufferId, uint32_t> buffer_size_list{memory_resource};
  unordered_map<BufferId, uint32_t> buffer_alignment_list{memory_resource};
  for (auto [buffer_id, buffer_config] : buffer_config_list) {
    auto [size, alignment] = configure_function(buffer_config);
    buffer_size_list.insert_or_assign(buffer_id, size);
    buffer_alignment_list.insert_or_assign(buffer_id, alignment);
  }
  return {buffer_size_list, buffer_alignment_list};
}
void RenderGraph::Build(const RenderGraphConfig& config, std::pmr::memory_resource* memory_resource_work) {
  render_pass_num_ = config.GetRenderPassNum();
  std::tie(buffer_id_list_, render_pass_buffer_id_list_, render_pass_buffer_state_flag_list_) = InitBufferIdList(render_pass_num_, config.GetRenderPassBufferStateList(), memory_resource_);
  auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num_, buffer_id_list_, render_pass_buffer_id_list_, render_pass_buffer_state_flag_list_, memory_resource_work);
  auto buffer_name_id_map = GetBufferNameIdMap(render_pass_num_, config.GetRenderPassBufferStateList(), render_pass_buffer_id_list_, memory_resource_work);
  auto initial_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, config.GetBufferInitialStateList(), memory_resource_work);
  std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
  auto final_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, config.GetBufferFinalStateList(), memory_resource_work);
  std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
  render_pass_command_queue_type_list_ = vector<CommandQueueType>(config.GetRenderPassCommandQueueTypeList(), memory_resource_);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(render_pass_num_, render_pass_command_queue_type_list_, memory_resource_work, memory_resource_work);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list_, memory_resource_work, memory_resource_work);
  inter_queue_pass_dependency = RemoveRedundantDependencyFromSameQueuePredecessors(render_pass_num_, render_pass_command_queue_type_list_, std::move(inter_queue_pass_dependency), memory_resource_work);
  queue_signals_ = ConfigureQueueSignals(render_pass_num_, inter_queue_pass_dependency, render_pass_command_queue_type_list_, memory_resource_, memory_resource_work);
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = ConfigureTripleInterQueueDependency(render_pass_num_, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  auto buffer_id_name_map = CreateBufferIdNameMap(render_pass_num_, static_cast<uint32_t>(buffer_id_list_.size()), config.GetRenderPassBufferStateList(), render_pass_buffer_id_list_, memory_resource_);
  buffer_config_list_ = CreateBufferConfigList(buffer_id_list_,
                                               buffer_id_name_map,
                                               config.GetPrimaryBufferWidth(), config.GetPrimaryBufferHeight(),
                                               config.GetSwapchainBufferWidth(), config.GetSwapchainBufferHeight(),
                                               config.GetBufferSizeInfoList(),
                                               buffer_state_list,
                                               config.GetBufferDefaultClearValueList(),
                                               config.GetBufferDimensionTypeList(),
                                               config.GetBufferFormatList(),
                                               config.GetBufferDepthStencilFlagList(),
                                               memory_resource_);
  auto excluded_buffers = CollectExternalBufferIds(buffer_name_id_map, config.GetExternalBufferNameList(), memory_resource_);
  std::tie(buffer_size_list_, buffer_alignment_list_) = GetBufferSizeInfo(buffer_config_list_, config.GetBufferSizeInfoFunction(), memory_resource_work);
  auto [render_pass_new_buffer_list, render_pass_expired_buffer_list] = ConfigureRenderPassNewAndExpiredBuffers(render_pass_num_, buffer_id_list_, buffer_user_pass_list, memory_resource_work);
  unordered_map<BufferId, BufferId> renamed_buffers{memory_resource_work};
  std::tie(buffer_address_offset_list_, renamed_buffers, render_pass_before_memory_aliasing_list_, render_pass_after_memory_aliasing_list_) = ConfigureBufferAddressOffset(render_pass_num_, render_pass_buffer_id_list_, node_distance_map, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list_, buffer_size_list_, buffer_alignment_list_, excluded_buffers, memory_resource_work, memory_resource_work);
  buffer_id_list_ = UpdateBufferIdListUsingReusableBuffers(renamed_buffers, std::move(buffer_id_list_));
  render_pass_buffer_id_list_ = UpdateRenderPassBufferIdListUsingReusableBuffers(renamed_buffers, std::move(render_pass_buffer_id_list_));
  std::tie(buffer_state_list, buffer_user_pass_list) = UpdateBufferStateListAndUserPassListUsingReusableBuffers(renamed_buffers, std::move(buffer_state_list), std::move(buffer_user_pass_list));
  buffer_config_list_ = MergeReusedBufferConfigs(renamed_buffers, std::move(buffer_config_list_));
  std::tie(buffer_state_list, buffer_user_pass_list) = RevertBufferStateToInitialState(std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
  auto render_pass_command_queue_type_flag_list = ConvertToCommandQueueTypeFlagsList(render_pass_command_queue_type_list_, memory_resource_work);
  auto buffer_state_change_info_list_map = CreateBufferStateChangeInfoList(render_pass_command_queue_type_flag_list, node_distance_map, buffer_state_list, buffer_user_pass_list, memory_resource_work);
  std::tie(barriers_pre_pass_, barriers_post_pass_) = ConfigureBarriers(render_pass_num_, buffer_id_list_, buffer_state_change_info_list_map, memory_resource_);
}
#ifdef BUILD_WITH_TEST
static std::tuple<vector<BufferId>, unordered_map<BufferId, vector<vector<uint32_t>>>> CollectBufferDataForTest(const vector<vector<BufferId>>& render_pass_buffer_id_list, std::pmr::memory_resource* memory_resource) {
  vector<BufferId> buffer_id_list{memory_resource};
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{memory_resource};
  auto pass_num = static_cast<uint32_t>(render_pass_buffer_id_list.size());
  for (uint32_t pass_index = 0; pass_index < pass_num; pass_index++) {
    auto buffer_num = static_cast<uint32_t>(render_pass_buffer_id_list[pass_index].size());
    for (uint32_t buffer_index = 0; buffer_index < buffer_num; buffer_index++) {
      auto& buffer_id = render_pass_buffer_id_list[pass_index][buffer_index];
      if (!buffer_user_pass_list.contains(buffer_id)) {
        if (!buffer_id_list.empty()) {
          if (buffer_id_list.back() <= buffer_id) {
            buffer_id_list.push_back(buffer_id);
          } else {
            for (auto it = buffer_id_list.begin(); it != buffer_id_list.end(); it++) {
              if (*it >= buffer_id) {
                buffer_id_list.insert(it, buffer_id);
                break;
              }
            }
          }
        } else {
          buffer_id_list.push_back(buffer_id);
        }
        buffer_user_pass_list.insert_or_assign(buffer_id, vector<vector<uint32_t>>{memory_resource});
      }
      buffer_user_pass_list.at(buffer_id).push_back(vector<uint32_t>{memory_resource});
      buffer_user_pass_list.at(buffer_id).back().push_back(pass_index);
    }
  }
  return {buffer_id_list, buffer_user_pass_list};
}
#endif
}
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_offset_in_bytes_persistent = 0;
const uint32_t buffer_size_in_bytes_persistent = 16 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_persistent + buffer_size_in_bytes_persistent;
const uint32_t buffer_size_in_bytes_scene = 8 * 1024;
const uint32_t buffer_offset_in_bytes_frame = buffer_offset_in_bytes_scene + buffer_size_in_bytes_scene;
const uint32_t buffer_size_in_bytes_frame = 4 * 1024;
const uint32_t buffer_offset_in_bytes_work = buffer_offset_in_bytes_frame + buffer_size_in_bytes_frame;
const uint32_t buffer_size_in_bytes_work = 64 * 1024;
std::byte buffer[buffer_offset_in_bytes_work + buffer_size_in_bytes_work]{};
std::tuple<uint32_t, uint32_t> GetBufferSizeAndAlignmentImpl([[maybe_unused]]const illuminate::gfx::BufferConfig& config) {
  return {16, 8};
}
}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include "doctest/doctest.h"
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - single pass") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(node_distance_map.size() == 1);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 1);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - two pass (graphics only)") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(node_distance_map.size() == 2);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 1);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(node_distance_map.contains(1));
  CHECK(node_distance_map.at(1).size() == 2);
  CHECK(node_distance_map.at(1).contains(0));
  CHECK(node_distance_map.at(1).at(0) == -1);
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.size() == 2);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 2);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(node_distance_map.at(0).contains(1));
  CHECK(node_distance_map.at(0).at(1) == 1);
  CHECK(node_distance_map.contains(1));
  CHECK(node_distance_map.at(1).size() == 2);
  CHECK(node_distance_map.at(1).contains(0));
  CHECK(node_distance_map.at(1).at(0) == -1);
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - two independent pass (graphics+compute)") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(node_distance_map.size() == 2);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 1);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(node_distance_map.contains(1));
  CHECK(node_distance_map.at(1).size() == 1);
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics->compute)") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(node_distance_map.size() == 2);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 2);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(node_distance_map.at(0).contains(1));
  CHECK(node_distance_map.at(0).at(1) == 1);
  CHECK(node_distance_map.contains(1));
  CHECK(node_distance_map.at(1).size() == 2);
  CHECK(node_distance_map.at(1).contains(0));
  CHECK(node_distance_map.at(1).at(0) == -1);
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics->compute(+graphics))") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(node_distance_map.size() == 3);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 3);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(node_distance_map.at(0).contains(1));
  CHECK(node_distance_map.at(0).at(1) == 1);
  CHECK(node_distance_map.at(0).contains(2));
  CHECK(node_distance_map.at(0).at(2) == 1);
  CHECK(node_distance_map.contains(1));
  CHECK(node_distance_map.at(1).size() == 2);
  CHECK(node_distance_map.at(1).contains(0));
  CHECK(node_distance_map.at(1).at(0) == -1);
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
  CHECK(node_distance_map.contains(2));
  CHECK(node_distance_map.at(2).size() == 2);
  CHECK(node_distance_map.at(2).contains(0));
  CHECK(node_distance_map.at(2).at(0) == -1);
  CHECK(node_distance_map.at(2).contains(2));
  CHECK(node_distance_map.at(2).at(2) == 0);
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics+compute->graphics)") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(node_distance_map.size() == 3);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 2);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(node_distance_map.at(0).contains(2));
  CHECK(node_distance_map.at(0).at(2) == 1);
  CHECK(node_distance_map.contains(2));
  CHECK(node_distance_map.at(1).size() == 2);
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
  CHECK(node_distance_map.at(1).contains(2));
  CHECK(node_distance_map.at(1).at(2) == 1);
  CHECK(node_distance_map.contains(2));
  CHECK(node_distance_map.at(2).size() == 3);
  CHECK(node_distance_map.at(2).contains(0));
  CHECK(node_distance_map.at(2).at(0) == -1);
  CHECK(node_distance_map.at(2).contains(1));
  CHECK(node_distance_map.at(2).at(1) == -1);
  CHECK(node_distance_map.at(2).contains(2));
  CHECK(node_distance_map.at(2).at(2) == 0);
}
TEST_CASE("CreateNodeDistanceMapInSameCommandQueueType - dependent pass (graphics+compute->graphics+compute)") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(node_distance_map.size() == 5);
  CHECK(node_distance_map.contains(0));
  CHECK(node_distance_map.at(0).size() == 4);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(node_distance_map.at(0).contains(2));
  CHECK(node_distance_map.at(0).at(2) == 1);
  CHECK(node_distance_map.at(0).contains(3));
  CHECK(node_distance_map.at(0).at(3) == 1);
  CHECK(node_distance_map.at(0).contains(4));
  CHECK(node_distance_map.at(0).at(4) == 2);
  CHECK(node_distance_map.contains(1));
  CHECK(node_distance_map.at(1).size() == 4);
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
  CHECK(node_distance_map.at(1).contains(2));
  CHECK(node_distance_map.at(1).at(2) == 1);
  CHECK(node_distance_map.at(1).contains(3));
  CHECK(node_distance_map.at(1).at(3) == 1);
  CHECK(node_distance_map.at(1).contains(4));
  CHECK(node_distance_map.at(1).at(4) == 2);
  CHECK(node_distance_map.contains(2));
  CHECK(node_distance_map.at(2).size() == 3);
  CHECK(node_distance_map.at(2).contains(0));
  CHECK(node_distance_map.at(2).at(0) == -1);
  CHECK(node_distance_map.at(2).contains(1));
  CHECK(node_distance_map.at(2).at(1) == -1);
  CHECK(node_distance_map.at(2).contains(2));
  CHECK(node_distance_map.at(2).at(2) == 0);
  CHECK(node_distance_map.contains(3));
  CHECK(node_distance_map.at(3).size() == 4);
  CHECK(node_distance_map.at(3).contains(0));
  CHECK(node_distance_map.at(3).at(0) == -1);
  CHECK(node_distance_map.at(3).contains(1));
  CHECK(node_distance_map.at(3).at(1) == -1);
  CHECK(node_distance_map.at(3).contains(3));
  CHECK(node_distance_map.at(3).at(3) == 0);
  CHECK(node_distance_map.at(3).contains(4));
  CHECK(node_distance_map.at(3).at(4) == 1);
  CHECK(node_distance_map.contains(3));
  CHECK(node_distance_map.at(4).size() == 4);
  CHECK(node_distance_map.at(4).contains(0));
  CHECK(node_distance_map.at(4).at(0) == -2);
  CHECK(node_distance_map.at(4).contains(1));
  CHECK(node_distance_map.at(4).at(1) == -2);
  CHECK(node_distance_map.at(4).contains(3));
  CHECK(node_distance_map.at(4).at(3) == -1);
  CHECK(node_distance_map.at(4).contains(4));
  CHECK(node_distance_map.at(4).at(4) == 0);
}
TEST_CASE("ConfigureInterPassDependency") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<CommandQueueType> render_pass_command_queue_type_list{&memory_resource_work};
  render_pass_command_queue_type_list.push_back(CommandQueueType::kGraphics);
  render_pass_command_queue_type_list.push_back(CommandQueueType::kCompute);
  unordered_map<BufferId, vector<vector<uint32_t>>> buffer_user_pass_list{&memory_resource_work};
  auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.empty());
  buffer_user_pass_list.insert_or_assign(0, vector<vector<uint32_t>>{&memory_resource_work});
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(0);
  buffer_user_pass_list.at(0).push_back(vector<uint32_t>{&memory_resource_work});
  buffer_user_pass_list.at(0).back().push_back(1);
  inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  CHECK(inter_queue_pass_dependency.size() == 1);
  CHECK(inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.at(1).size() == 1);
  CHECK(inter_queue_pass_dependency.at(1).contains(0));
}
TEST_CASE("ConfigureInterPassDependency - multiple buffers") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.size() == 2);
  CHECK(inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.at(1).size() == 1);
  CHECK(inter_queue_pass_dependency.at(1).contains(0));
  CHECK(inter_queue_pass_dependency.contains(2));
  CHECK(inter_queue_pass_dependency.at(2).size() == 1);
  CHECK(inter_queue_pass_dependency.at(2).contains(0));
}
TEST_CASE("ConfigureInterPassDependency - remove processed dependency from ancestor pass") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.size() == 1);
  CHECK(inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.at(1).size() == 1);
  CHECK(inter_queue_pass_dependency.at(1).contains(0));
}
TEST_CASE("ConfigureInterPassDependency - remove processed dependency from ancestor pass with multiple buffers") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.size() == 1);
  CHECK(inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.at(1).size() == 1);
  CHECK(inter_queue_pass_dependency.at(1).contains(0));
  CHECK(!inter_queue_pass_dependency.contains(2));
}
TEST_CASE("ConfigureInterPassDependency - remove processed dependency from ancestor pass with different queue type") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.size() == 2);
  CHECK(inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.at(1).size() == 1);
  CHECK(inter_queue_pass_dependency.at(1).contains(0));
  CHECK(inter_queue_pass_dependency.contains(3));
  CHECK(inter_queue_pass_dependency.at(3).size() == 1);
  CHECK(inter_queue_pass_dependency.at(3).contains(2));
  CHECK(!inter_queue_pass_dependency.contains(0));
  CHECK(!inter_queue_pass_dependency.contains(2));
  CHECK(!inter_queue_pass_dependency.contains(4));
}
TEST_CASE("ConfigureInterPassDependency - 3 queue test") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.size() == 2);
  CHECK(inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.at(1).size() == 1);
  CHECK(inter_queue_pass_dependency.at(1).contains(0));
  CHECK(inter_queue_pass_dependency.contains(2));
  CHECK(inter_queue_pass_dependency.at(2).size() == 1);
  CHECK(inter_queue_pass_dependency.at(2).contains(0));
}
TEST_CASE("ConfigureInterPassDependency - dependent on multiple queue") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.size() == 1);
  CHECK(inter_queue_pass_dependency.contains(2));
  CHECK(inter_queue_pass_dependency.at(2).size() == 2);
  CHECK(inter_queue_pass_dependency.at(2).contains(0));
  CHECK(inter_queue_pass_dependency.at(2).contains(1));
}
TEST_CASE("ConfigureInterPassDependency - dependent on single queue") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.size() == 2);
  CHECK(inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.at(1).size() == 1);
  CHECK(inter_queue_pass_dependency.at(1).contains(0));
  CHECK(inter_queue_pass_dependency.contains(2));
  CHECK(inter_queue_pass_dependency.at(2).size() == 1);
  CHECK(inter_queue_pass_dependency.at(2).contains(1));
}
TEST_CASE("ConfigureInterPassDependency - remove same queue dependency") {
  using namespace illuminate;
  using namespace illuminate::gfx;
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
  CHECK(inter_queue_pass_dependency.empty());
}
TEST_CASE("barrier for load from srv") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  render_graph_config.SetBufferSizeInfoFunction(GetBufferSizeAndAlignmentImpl);
  CHECK(render_graph_config.GetRenderPassNum() == 0);
  auto render_pass_id = render_graph_config.CreateNewRenderPass({.pass_name = StrId("draw"), .command_queue_type = CommandQueueType::kGraphics});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag});
  CHECK(render_graph_config.GetRenderPassNum() == 1);
  auto prev_render_pass_id = render_pass_id;
  render_pass_id = render_graph_config.CreateNewRenderPass({.pass_name = StrId("copy"), .command_queue_type = CommandQueueType::kGraphics});
  CHECK(render_graph_config.GetRenderPassNum() == 2);
  CHECK(render_pass_id != prev_render_pass_id);
  render_graph_config.AppendRenderPassBufferConfig(render_pass_id, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_id, {StrId("swapchain"),  kBufferStateFlagRtv, kWriteFlag});
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
    auto buffer_name_id_map = GetBufferNameIdMap(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, &memory_resource_work);
    auto initial_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferInitialStateList(), &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    auto final_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferFinalStateList(), &memory_resource_work);
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
  RenderGraph render_graph(&memory_resource_scene);
  render_graph.Build(render_graph_config, &memory_resource_work);
  CHECK(render_graph.GetRenderPassNum() == 2);
  {
    auto& buffer_list = render_graph.GetBufferIdList();
    CHECK(buffer_list.size() == 2);
    CHECK(buffer_list[0] == 0);
    CHECK(buffer_list[1] == 1);
  }
  auto& barriers_prepass = render_graph.GetBarriersPrePass();
  auto& barriers_postpass = render_graph.GetBarriersPostPass();
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
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  render_graph_config.SetBufferSizeInfoFunction(GetBufferSizeAndAlignmentImpl);
  auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("draw"), .command_queue_type = CommandQueueType::kCompute});
  CHECK(render_graph_config.GetRenderPassIndex(StrId("draw")) == 0);
  render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagUav, kWriteFlag});
  render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("present"), .command_queue_type = CommandQueueType::kGraphics});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagUav, kReadFlag});
  render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("swapchain"), kBufferStateFlagRtv, kWriteFlag});
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  CHECK(render_graph_config.GetRenderPassIndex(StrId("draw")) == 0);
  CHECK(render_graph_config.GetRenderPassIndex(StrId("present")) == 1);
  CHECK(render_graph_config.GetRenderPassIndex(StrId("present")) == render_pass_index);
  RenderGraph render_graph(&memory_resource_scene);
  render_graph.Build(render_graph_config, &memory_resource_work);
  auto& barriers_prepass = render_graph.GetBarriersPrePass();
  auto& barriers_postpass = render_graph.GetBarriersPostPass();
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
  CHECK(barriers_postpass[1].size() == 2);
  CHECK(barriers_postpass[1][0].buffer_id == 0);
  CHECK(barriers_postpass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierUav>(barriers_postpass[1][0].params));
  CHECK(barriers_postpass[1][1].buffer_id == 1);
  CHECK(barriers_postpass[1][1].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_after  == kBufferStateFlagPresent);
  auto& queue_signals = render_graph.GetQueueSignals();
  CHECK(queue_signals.size() == 2);
  CHECK(queue_signals.contains(0));
  CHECK(queue_signals.at(0).size() == 1);
  CHECK(queue_signals.at(0).contains(1));
  CHECK(queue_signals.contains(1));
  CHECK(queue_signals.at(1).size() == 1);
  CHECK(queue_signals.at(1).contains(0));
}
TEST_CASE("buffer reuse") {
  using namespace illuminate;
  using namespace illuminate::core;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  RenderGraphConfig render_graph_config(&memory_resource_scene);
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("0"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag});
  }
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("1"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag});
  }
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("2"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagRtv, kWriteFlag});
  }
  {
    auto render_pass_index = render_graph_config.CreateNewRenderPass({.pass_name = StrId("present"), .command_queue_type = CommandQueueType::kGraphics});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("mainbuffer"), kBufferStateFlagSrvPsOnly, kReadFlag});
    render_graph_config.AppendRenderPassBufferConfig(render_pass_index, {StrId("swapchain"), kBufferStateFlagRtv, kWriteFlag});
  }
  render_graph_config.AddBufferInitialState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"),  kBufferStateFlagPresent);
  render_graph_config.SetBufferSizeInfoFunction(GetBufferSizeAndAlignmentImpl);
  render_graph_config.AddExternalBufferName(StrId("swapchain"));
  CHECK(render_graph_config.GetExternalBufferNameList().size() == 1);
  CHECK(render_graph_config.GetExternalBufferNameList().contains(StrId("swapchain")));
  unordered_map<BufferId, unordered_map<BufferId, int32_t>> node_distance_map;
  {
    auto render_pass_num = render_graph_config.GetRenderPassNum();
    auto [buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list] = InitBufferIdList(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), &memory_resource_work);
    auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num, buffer_id_list, render_pass_buffer_id_list, render_pass_buffer_state_flag_list, &memory_resource_work);
    auto buffer_name_id_map = GetBufferNameIdMap(render_pass_num, render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, &memory_resource_work);
    auto initial_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferInitialStateList(), &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    auto final_state_flag_list = ConvertBufferNameToBufferIdForBufferStateFlagList(buffer_name_id_map, render_graph_config.GetBufferFinalStateList(), &memory_resource_work);
    std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    // std::tie(buffer_state_list, buffer_user_pass_list) = RevertBufferStateToInitialState(std::move(buffer_state_list), std::move(buffer_user_pass_list), &memory_resource_work);
    auto render_pass_command_queue_type_list = vector<CommandQueueType>(render_graph_config.GetRenderPassCommandQueueTypeList(), &memory_resource_work);
    node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(render_pass_num, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
    auto inter_queue_pass_dependency = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
    inter_queue_pass_dependency = RemoveRedundantDependencyFromSameQueuePredecessors(render_pass_num, render_pass_command_queue_type_list, std::move(inter_queue_pass_dependency), &memory_resource_work);
    node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
    node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
    auto buffer_config_list = CreateBufferConfigList(buffer_id_list,
                                                     CreateBufferIdNameMap(render_pass_num, static_cast<uint32_t>(buffer_id_list.size()), render_graph_config.GetRenderPassBufferStateList(), render_pass_buffer_id_list, &memory_resource_work),
                                                     render_graph_config.GetPrimaryBufferWidth(), render_graph_config.GetPrimaryBufferHeight(),
                                                     render_graph_config.GetSwapchainBufferWidth(), render_graph_config.GetSwapchainBufferHeight(),
                                                     render_graph_config.GetBufferSizeInfoList(),
                                                     buffer_state_list,
                                                     render_graph_config.GetBufferDefaultClearValueList(),
                                                     render_graph_config.GetBufferDimensionTypeList(),
                                                     render_graph_config.GetBufferFormatList(),
                                                     render_graph_config.GetBufferDepthStencilFlagList(),
                                                     &memory_resource_work);
    unordered_set<BufferId> excluded_buffers{&memory_resource_work};
    excluded_buffers.insert(3);
    auto [render_pass_new_buffer_list, render_pass_expired_buffer_list] = ConfigureRenderPassNewAndExpiredBuffers(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), buffer_id_list, buffer_user_pass_list, &memory_resource_work);
    unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
    buffer_size_list.insert_or_assign(0, 8);
    buffer_size_list.insert_or_assign(1, 8);
    buffer_size_list.insert_or_assign(2, 8);
    buffer_size_list.insert_or_assign(3, 8);
    buffer_size_list.insert_or_assign(4, 8);
    unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
    buffer_alignment_list.insert_or_assign(0, 8);
    buffer_alignment_list.insert_or_assign(1, 8);
    buffer_alignment_list.insert_or_assign(2, 8);
    buffer_alignment_list.insert_or_assign(3, 8);
    buffer_alignment_list.insert_or_assign(4, 8);
    auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_buffer_id_list, node_distance_map, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, excluded_buffers, &memory_resource_work, &memory_resource_work);
    CHECK(renamed_buffers.size() == 1);
    CHECK(renamed_buffers.contains(2));
    CHECK(renamed_buffers.at(2) == 0);
    CHECK(render_pass_before_memory_aliasing_list.empty());
    CHECK(render_pass_after_memory_aliasing_list.empty());
    buffer_id_list = UpdateBufferIdListUsingReusableBuffers(renamed_buffers, std::move(buffer_id_list));
    CHECK(buffer_id_list.size() == 3);
    CHECK(buffer_id_list[0] == 0);
    CHECK(buffer_id_list[1] == 1);
    CHECK(buffer_id_list[2] == 3);
    render_pass_buffer_id_list = UpdateRenderPassBufferIdListUsingReusableBuffers(renamed_buffers, std::move(render_pass_buffer_id_list));
    CHECK(render_pass_buffer_id_list[0][0] == 0);
    CHECK(render_pass_buffer_id_list[1][0] == 0);
    CHECK(render_pass_buffer_id_list[1][1] == 1);
    CHECK(render_pass_buffer_id_list[2][0] == 1);
    CHECK(render_pass_buffer_id_list[2][1] == 0);
    CHECK(render_pass_buffer_id_list[3][0] == 0);
    CHECK(render_pass_buffer_id_list[3][1] == 3);
    std::tie(buffer_state_list, buffer_user_pass_list) = UpdateBufferStateListAndUserPassListUsingReusableBuffers(renamed_buffers, std::move(buffer_state_list), std::move(buffer_user_pass_list));
    CHECK(buffer_state_list.size() == 3);
    CHECK(!buffer_state_list.contains(2));
    CHECK(buffer_state_list.at(0).size() == 4);
    CHECK(buffer_state_list.at(0)[0] == kBufferStateFlagRtv);
    CHECK(buffer_state_list.at(0)[1] == kBufferStateFlagSrvPsOnly);
    CHECK(buffer_state_list.at(0)[2] == kBufferStateFlagRtv);
    CHECK(buffer_state_list.at(0)[3] == kBufferStateFlagSrvPsOnly);
    CHECK(buffer_user_pass_list.size() == 3);
    CHECK(!buffer_user_pass_list.contains(2));
    CHECK(buffer_user_pass_list.at(0).size() == 4);
    CHECK(buffer_user_pass_list.at(0)[0].size() == 1);
    CHECK(buffer_user_pass_list.at(0)[0][0] == 0);
    CHECK(buffer_user_pass_list.at(0)[1].size() == 1);
    CHECK(buffer_user_pass_list.at(0)[1][0] == 1);
    CHECK(buffer_user_pass_list.at(0)[2].size() == 1);
    CHECK(buffer_user_pass_list.at(0)[2][0] == 2);
    CHECK(buffer_user_pass_list.at(0)[3].size() == 1);
    CHECK(buffer_user_pass_list.at(0)[3][0] == 3);
    unordered_map<BufferId, BufferStateFlags> flag_copy{&memory_resource_work};
    for (auto& [id, config] : buffer_config_list) {
      flag_copy.insert_or_assign(id, config.state_flags);
    }
    buffer_config_list = MergeReusedBufferConfigs(renamed_buffers, std::move(buffer_config_list));
    CHECK(buffer_config_list.size() == 3);
    CHECK(!buffer_config_list.contains(2));
    for (auto& [id, config] : buffer_config_list) {
      CHECK(config.state_flags == flag_copy.at(id));
    }
  }
  RenderGraph render_graph(&memory_resource_scene);
  render_graph.Build(render_graph_config, &memory_resource_work);
  CHECK(render_graph.GetBufferIdList().size() == 3);
  auto& render_pass_buffer_id_list = render_graph.GetRenderPassBufferIdList();
  CHECK(render_pass_buffer_id_list[0][0] == render_pass_buffer_id_list[1][0]);
  CHECK(render_pass_buffer_id_list[1][1] == render_pass_buffer_id_list[2][0]);
  CHECK(render_pass_buffer_id_list[2][1] == render_pass_buffer_id_list[0][0]);
  CHECK(render_pass_buffer_id_list[2][1] == render_pass_buffer_id_list[3][0]);
  CHECK(render_pass_buffer_id_list[3][1] != render_pass_buffer_id_list[0][0]);
  CHECK(render_pass_buffer_id_list[3][1] != render_pass_buffer_id_list[1][1]);
  CHECK(render_graph.GetQueueSignals().empty());
  auto& barriers_prepass = render_graph.GetBarriersPrePass();
  CHECK(barriers_prepass.size() == 4);
  CHECK(barriers_prepass[0].size() == 1);
  CHECK(barriers_prepass[0][0].buffer_id == 3);
  CHECK(barriers_prepass[0][0].split_type == BarrierSplitType::kBegin);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[0][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers_prepass[0][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_prepass[1].size() == 1);
  CHECK(barriers_prepass[1][0].buffer_id == 0);
  CHECK(barriers_prepass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[1][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_prepass[1][0].params).state_after  == kBufferStateFlagSrvPsOnly);
  CHECK(barriers_prepass[2].size() == 2);
  CHECK(barriers_prepass[2][0].buffer_id == 0);
  CHECK(barriers_prepass[2][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[2][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][0].params).state_before == kBufferStateFlagSrvPsOnly);
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_prepass[2][1].buffer_id == 1);
  CHECK(barriers_prepass[2][1].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[2][1].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][1].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_prepass[2][1].params).state_after  == kBufferStateFlagSrvPsOnly);
  CHECK(barriers_prepass[3].size() == 2);
  CHECK(barriers_prepass[3][0].buffer_id == 0);
  CHECK(barriers_prepass[3][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[3][0].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][0].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][0].params).state_after  == kBufferStateFlagSrvPsOnly);
  CHECK(barriers_prepass[3][1].buffer_id == 3);
  CHECK(barriers_prepass[3][1].split_type == BarrierSplitType::kEnd);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_prepass[3][1].params));
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][1].params).state_before == kBufferStateFlagPresent);
  CHECK(std::get<BarrierTransition>(barriers_prepass[3][1].params).state_after  == kBufferStateFlagRtv);
  auto& barriers_postpass = render_graph.GetBarriersPostPass();
  CHECK(barriers_postpass.size() == 4);
  CHECK(barriers_postpass[0].empty());
  CHECK(barriers_postpass[1].empty());
  CHECK(barriers_postpass[2].size() == 1);
  CHECK(barriers_postpass[2][0].buffer_id == 1);
  CHECK(barriers_postpass[2][0].split_type == BarrierSplitType::kBegin);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[2][0].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[2][0].params).state_before == kBufferStateFlagSrvPsOnly);
  CHECK(std::get<BarrierTransition>(barriers_postpass[2][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass[3].size() == 3);
  CHECK(barriers_postpass[3][0].buffer_id == 0);
  CHECK(barriers_postpass[3][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[3][0].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][0].params).state_before == kBufferStateFlagSrvPsOnly);
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][0].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass[3][1].buffer_id == 1);
  CHECK(barriers_postpass[3][1].split_type == BarrierSplitType::kEnd);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[3][1].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][1].params).state_before == kBufferStateFlagSrvPsOnly);
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][1].params).state_after  == kBufferStateFlagRtv);
  CHECK(barriers_postpass[3][2].buffer_id == 3);
  CHECK(barriers_postpass[3][2].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[3][2].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][2].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_postpass[3][2].params).state_after  == kBufferStateFlagPresent);
}
TEST_CASE("memory aliasing") {
  using namespace illuminate;
  using namespace illuminate::core;
  using namespace illuminate::gfx;
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
  CHECK(render_pass_new_buffer_list.size() == 5);
  CHECK(render_pass_new_buffer_list[0].size() == 1);
  CHECK(render_pass_new_buffer_list[0][0] == 0);
  CHECK(render_pass_new_buffer_list[1].size() == 1);
  CHECK(render_pass_new_buffer_list[1][0] == 1);
  CHECK(render_pass_new_buffer_list[2].size() == 1);
  CHECK(render_pass_new_buffer_list[2][0] == 2);
  CHECK(render_pass_new_buffer_list[3].size() == 1);
  CHECK(render_pass_new_buffer_list[3][0] == 3);
  CHECK(render_pass_new_buffer_list[4].size() == 1);
  CHECK(render_pass_new_buffer_list[4][0] == 4);
  CHECK(render_pass_expired_buffer_list.size() == 5);
  CHECK(render_pass_expired_buffer_list[0].empty());
  CHECK(render_pass_expired_buffer_list[1].empty());
  CHECK(render_pass_expired_buffer_list[2].size() == 1);
  CHECK(render_pass_expired_buffer_list[2][0] == 0);
  CHECK(render_pass_expired_buffer_list[3].size() == 2);
  CHECK(render_pass_expired_buffer_list[3][0] == 1);
  CHECK(render_pass_expired_buffer_list[3][1] == 2);
  CHECK(render_pass_expired_buffer_list[4].size() == 2);
  CHECK(render_pass_expired_buffer_list[4][0] == 3);
  CHECK(render_pass_expired_buffer_list[4][1] == 4);
  unordered_map<BufferId, BufferConfig> buffer_config_list{&memory_resource_work};
  buffer_config_list.insert_or_assign(0, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(1, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(2, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(3, BufferConfig{.width=1,.height=2,.state_flags=MergeBufferStateFlags(kBufferStateFlagRtv,kBufferStateFlagSrvPsOnly),.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(4, BufferConfig{.width=3,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
  buffer_size_list.insert_or_assign(0, 8);
  buffer_size_list.insert_or_assign(1, 8);
  buffer_size_list.insert_or_assign(2, 8);
  buffer_size_list.insert_or_assign(3, 8);
  buffer_size_list.insert_or_assign(4, 16);
  unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
  buffer_alignment_list.insert_or_assign(0, 8);
  buffer_alignment_list.insert_or_assign(1, 8);
  buffer_alignment_list.insert_or_assign(2, 8);
  buffer_alignment_list.insert_or_assign(3, 8);
  buffer_alignment_list.insert_or_assign(4, 8);
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_command_queue_type_list, &memory_resource_work, &memory_resource_work);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_buffer_id_list, node_distance_map, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, &memory_resource_work, &memory_resource_work);
  CHECK(buffer_address_offset_list.size() == 5);
  CHECK(buffer_address_offset_list.at(0) == 0);
  CHECK(buffer_address_offset_list.at(1) == 8);
  CHECK(buffer_address_offset_list.at(2) == 16);
  CHECK(buffer_address_offset_list.at(3) == 0);
  CHECK(buffer_address_offset_list.at(4) == 8);
  CHECK(renamed_buffers.size() == 1);
  CHECK(renamed_buffers.at(3) == 0);
  CHECK(render_pass_before_memory_aliasing_list.size() == 1);
  CHECK(!render_pass_before_memory_aliasing_list.contains(0));
  CHECK(!render_pass_before_memory_aliasing_list.contains(1));
  CHECK(!render_pass_before_memory_aliasing_list.contains(2));
  CHECK(render_pass_before_memory_aliasing_list.contains(3));
  CHECK(!render_pass_before_memory_aliasing_list.contains(4));
  CHECK(render_pass_before_memory_aliasing_list.at(3)[0] == 1);
  CHECK(render_pass_after_memory_aliasing_list.size() == 1);
  CHECK(render_pass_after_memory_aliasing_list.at(3)[0] == 4);
}
TEST_CASE("ConfigureBufferValidPassList w/async compute") {
  using namespace illuminate;
  using namespace illuminate::core;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<vector<BufferId>> render_pass_buffer_id_list{&memory_resource_work};
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(0);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(1);
  render_pass_buffer_id_list.back().push_back(7);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(1);
  render_pass_buffer_id_list.back().push_back(2);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(3);
  render_pass_buffer_id_list.back().push_back(8);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(3);
  render_pass_buffer_id_list.back().push_back(4);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(4);
  render_pass_buffer_id_list.back().push_back(5);
  render_pass_buffer_id_list.push_back(vector<BufferId>{&memory_resource_work});
  render_pass_buffer_id_list.back().push_back(5);
  render_pass_buffer_id_list.back().push_back(6);
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
  CHECK(inter_queue_pass_dependency.size() == 3);
  CHECK(!inter_queue_pass_dependency.contains(0));
  CHECK(!inter_queue_pass_dependency.contains(1));
  CHECK(inter_queue_pass_dependency.contains(2));
  CHECK(inter_queue_pass_dependency.at(2).size() == 1);
  CHECK(inter_queue_pass_dependency.at(2).contains(1));
  CHECK(!inter_queue_pass_dependency.contains(3));
  CHECK(inter_queue_pass_dependency.contains(4));
  CHECK(inter_queue_pass_dependency.at(4).size() == 1);
  CHECK(inter_queue_pass_dependency.at(4).contains(3));
  CHECK(!inter_queue_pass_dependency.contains(5));
  CHECK(inter_queue_pass_dependency.contains(6));
  CHECK(inter_queue_pass_dependency.at(6).size() == 1);
  CHECK(inter_queue_pass_dependency.at(6).contains(5));
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  node_distance_map = ConfigureTripleInterQueueDependency(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  CHECK(node_distance_map.at(0).size() == 3);
  CHECK(node_distance_map.at(0).contains(0));
  CHECK(node_distance_map.at(0).at(0) == 0);
  CHECK(!node_distance_map.at(0).contains(1));
  CHECK(node_distance_map.at(0).contains(2));
  CHECK(node_distance_map.at(0).at(2) == 1);
  CHECK(!node_distance_map.at(0).contains(3));
  CHECK(!node_distance_map.at(0).contains(4));
  CHECK(!node_distance_map.at(0).contains(5));
  CHECK(node_distance_map.at(0).contains(6));
  CHECK(node_distance_map.at(0).at(6) == 2);
  CHECK(node_distance_map.at(1).size() == 6);
  CHECK(!node_distance_map.at(1).contains(0));
  CHECK(node_distance_map.at(1).contains(1));
  CHECK(node_distance_map.at(1).at(1) == 0);
  CHECK(node_distance_map.at(1).contains(2));
  CHECK(node_distance_map.at(1).at(2) == 1);
  CHECK(node_distance_map.at(1).contains(3));
  CHECK(node_distance_map.at(1).at(3) == 1);
  CHECK(node_distance_map.at(1).contains(4));
  CHECK(node_distance_map.at(1).at(4) == 2);
  CHECK(node_distance_map.at(1).contains(5));
  CHECK(node_distance_map.at(1).at(5) == 3);
  CHECK(node_distance_map.at(1).contains(6));
  CHECK(node_distance_map.at(1).at(6) == 2);
  CHECK(node_distance_map.at(2).size() == 4);
  CHECK(node_distance_map.at(2).contains(0));
  CHECK(node_distance_map.at(2).at(0) == -1);
  CHECK(node_distance_map.at(2).contains(1));
  CHECK(node_distance_map.at(2).at(1) == -1);
  CHECK(node_distance_map.at(2).contains(2));
  CHECK(node_distance_map.at(2).at(2) == 0);
  CHECK(!node_distance_map.at(2).contains(3));
  CHECK(!node_distance_map.at(2).contains(4));
  CHECK(!node_distance_map.at(2).contains(5));
  CHECK(node_distance_map.at(2).contains(6));
  CHECK(node_distance_map.at(2).at(6) == 1);
  CHECK(node_distance_map.at(3).size() == 5);
  CHECK(!node_distance_map.at(3).contains(0));
  CHECK(node_distance_map.at(3).contains(1));
  CHECK(node_distance_map.at(3).at(1) == -1);
  CHECK(!node_distance_map.at(3).contains(2));
  CHECK(node_distance_map.at(3).contains(3));
  CHECK(node_distance_map.at(3).at(3) == 0);
  CHECK(node_distance_map.at(3).contains(4));
  CHECK(node_distance_map.at(3).at(4) == 1);
  CHECK(node_distance_map.at(3).contains(5));
  CHECK(node_distance_map.at(3).at(5) == 2);
  CHECK(node_distance_map.at(3).contains(6));
  CHECK(node_distance_map.at(3).at(6) == 3);
  CHECK(node_distance_map.at(4).size() == 5);
  CHECK(!node_distance_map.at(4).contains(0));
  CHECK(node_distance_map.at(4).contains(1));
  CHECK(node_distance_map.at(4).at(1) == -2);
  CHECK(!node_distance_map.at(4).contains(2));
  CHECK(node_distance_map.at(4).contains(3));
  CHECK(node_distance_map.at(4).at(3) == -1);
  CHECK(node_distance_map.at(4).contains(4));
  CHECK(node_distance_map.at(4).at(4) == 0);
  CHECK(node_distance_map.at(4).contains(5));
  CHECK(node_distance_map.at(4).at(5) == 1);
  CHECK(node_distance_map.at(4).contains(6));
  CHECK(node_distance_map.at(4).at(6) == 2);
  CHECK(node_distance_map.at(5).size() == 5);
  CHECK(!node_distance_map.at(5).contains(0));
  CHECK(node_distance_map.at(5).contains(1));
  CHECK(node_distance_map.at(5).at(1) == -3);
  CHECK(!node_distance_map.at(5).contains(2));
  CHECK(node_distance_map.at(5).contains(3));
  CHECK(node_distance_map.at(5).at(3) == -2);
  CHECK(node_distance_map.at(5).contains(4));
  CHECK(node_distance_map.at(5).at(4) == -1);
  CHECK(node_distance_map.at(5).contains(5));
  CHECK(node_distance_map.at(5).at(5) == 0);
  CHECK(node_distance_map.at(5).contains(6));
  CHECK(node_distance_map.at(5).at(6) == 1);
  CHECK(node_distance_map.at(6).size() == 7);
  CHECK(node_distance_map.at(6).contains(0));
  CHECK(node_distance_map.at(6).contains(1));
  CHECK(node_distance_map.at(6).contains(2));
  CHECK(node_distance_map.at(6).contains(3));
  CHECK(node_distance_map.at(6).contains(4));
  CHECK(node_distance_map.at(6).contains(5));
  CHECK(node_distance_map.at(6).contains(6));
  CHECK(node_distance_map.at(6).contains(0));
  CHECK(node_distance_map.at(6).at(0) == -2);
  CHECK(node_distance_map.at(6).at(1) == -2);
  CHECK(node_distance_map.at(6).at(2) == -1);
  CHECK(node_distance_map.at(6).at(3) == -3);
  CHECK(node_distance_map.at(6).at(4) == -2);
  CHECK(node_distance_map.at(6).at(5) == -1);
  CHECK(node_distance_map.at(6).at(6) == 0);
  auto [render_pass_new_buffer_list, render_pass_expired_buffer_list] = ConfigureRenderPassNewAndExpiredBuffers(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), buffer_id_list, buffer_user_pass_list, &memory_resource_work);
  CHECK(render_pass_new_buffer_list.size() == 7);
  CHECK(render_pass_new_buffer_list[0].size() == 1);
  CHECK(render_pass_new_buffer_list[0][0] == 0);
  CHECK(render_pass_new_buffer_list[1].size() == 2);
  CHECK(render_pass_new_buffer_list[1][0] == 1);
  CHECK(render_pass_new_buffer_list[1][1] == 7);
  CHECK(render_pass_new_buffer_list[2].size() == 1);
  CHECK(render_pass_new_buffer_list[2][0] == 2);
  CHECK(render_pass_new_buffer_list[3].size() == 2);
  CHECK(render_pass_new_buffer_list[3][0] == 3);
  CHECK(render_pass_new_buffer_list[3][1] == 8);
  CHECK(render_pass_new_buffer_list[4].size() == 1);
  CHECK(render_pass_new_buffer_list[4][0] == 4);
  CHECK(render_pass_new_buffer_list[5].size() == 1);
  CHECK(render_pass_new_buffer_list[5][0] == 5);
  CHECK(render_pass_new_buffer_list[6].size() == 1);
  CHECK(render_pass_new_buffer_list[6][0] == 6);
  CHECK(render_pass_expired_buffer_list.size() == 7);
  CHECK(render_pass_expired_buffer_list[0].size() == 1);
  CHECK(render_pass_expired_buffer_list[0][0] == 0);
  CHECK(render_pass_expired_buffer_list[1].size() == 1);
  CHECK(render_pass_expired_buffer_list[1][0] == 7);
  CHECK(render_pass_expired_buffer_list[2].size() == 2);
  CHECK(render_pass_expired_buffer_list[2][0] == 1);
  CHECK(render_pass_expired_buffer_list[2][1] == 2);
  CHECK(render_pass_expired_buffer_list[3].size() == 1);
  CHECK(render_pass_expired_buffer_list[3][0] == 8);
  CHECK(render_pass_expired_buffer_list[4].size() == 1);
  CHECK(render_pass_expired_buffer_list[4][0] == 3);
  CHECK(render_pass_expired_buffer_list[5].size() == 1);
  CHECK(render_pass_expired_buffer_list[5][0] == 4);
  CHECK(render_pass_expired_buffer_list[6].size() == 2);
  CHECK(render_pass_expired_buffer_list[6][0] == 5);
  CHECK(render_pass_expired_buffer_list[6][1] == 6);
  unordered_map<BufferId, BufferConfig> buffer_config_list{&memory_resource_work};
  buffer_config_list.insert_or_assign(0, BufferConfig{.width=1,.height=2,.state_flags=kBufferStateFlagRtv,.initial_state_flags=kBufferStateFlagRtv,.clear_value=GetClearValueDefaultColorBuffer(),.format=BufferFormat::kR8G8B8A8Unorm,.depth_stencil_flag=DepthStencilFlag::kDefault,});
  buffer_config_list.insert_or_assign(1, buffer_config_list.at(0));
  buffer_config_list.insert_or_assign(2, buffer_config_list.at(0));
  buffer_config_list.insert_or_assign(3, buffer_config_list.at(0));
  buffer_config_list.insert_or_assign(4, buffer_config_list.at(0));
  buffer_config_list.insert_or_assign(5, buffer_config_list.at(0));
  buffer_config_list.insert_or_assign(6, buffer_config_list.at(0));
  buffer_config_list.insert_or_assign(7, buffer_config_list.at(0));
  buffer_config_list.insert_or_assign(8, buffer_config_list.at(0));
  unordered_map<BufferId, uint32_t> buffer_size_list{&memory_resource_work};
  buffer_size_list.insert_or_assign(0, 8);
  buffer_size_list.insert_or_assign(1, 8);
  buffer_size_list.insert_or_assign(2, 8);
  buffer_size_list.insert_or_assign(3, 8);
  buffer_size_list.insert_or_assign(4, 8);
  buffer_size_list.insert_or_assign(5, 8);
  buffer_size_list.insert_or_assign(6, 8);
  buffer_size_list.insert_or_assign(7, 8);
  buffer_size_list.insert_or_assign(8, 8);
  unordered_map<BufferId, uint32_t> buffer_alignment_list{&memory_resource_work};
  buffer_alignment_list.insert_or_assign(0, 8);
  buffer_alignment_list.insert_or_assign(1, 8);
  buffer_alignment_list.insert_or_assign(2, 8);
  buffer_alignment_list.insert_or_assign(3, 8);
  buffer_alignment_list.insert_or_assign(4, 8);
  buffer_alignment_list.insert_or_assign(5, 8);
  buffer_alignment_list.insert_or_assign(6, 8);
  buffer_alignment_list.insert_or_assign(7, 8);
  buffer_alignment_list.insert_or_assign(8, 8);
  auto [buffer_address_offset_list, renamed_buffers, render_pass_before_memory_aliasing_list, render_pass_after_memory_aliasing_list] = ConfigureBufferAddressOffset(static_cast<uint32_t>(render_pass_command_queue_type_list.size()), render_pass_buffer_id_list, node_distance_map, render_pass_new_buffer_list, render_pass_expired_buffer_list, buffer_config_list, buffer_size_list, buffer_alignment_list, {}, &memory_resource_work, &memory_resource_work);
  CHECK(buffer_address_offset_list.size() == 9);
  CHECK(buffer_address_offset_list.at(0) == 0);
  CHECK(buffer_address_offset_list.at(1) == 8);
  CHECK(buffer_address_offset_list.at(2) == 0);
  CHECK(buffer_address_offset_list.at(3) == 16);
  CHECK(buffer_address_offset_list.at(4) == 24);
  CHECK(buffer_address_offset_list.at(5) == 16);
  CHECK(buffer_address_offset_list.at(6) == 0);
  CHECK(buffer_address_offset_list.at(7) == 16);
  CHECK(buffer_address_offset_list.at(8) == 24);
  CHECK(renamed_buffers.size() == 5);
  CHECK(renamed_buffers.at(2) == 0);
  CHECK(renamed_buffers.at(6) == 2);
  CHECK(renamed_buffers.at(5) == 3);
  CHECK(renamed_buffers.at(7) == 3);
  CHECK(renamed_buffers.at(8) == 4);
  // renamed_buffers == CombineRenamedBufferDuplicativeBufferIds(std::move(renamed_buffers), &memory_resource_work); // TODO
  CHECK(render_pass_before_memory_aliasing_list.empty());
  CHECK(render_pass_after_memory_aliasing_list.empty());
  // TODO check update renamed buffer
}
TEST_CASE("buffer allocation address calc") {
  using namespace illuminate;
  using namespace illuminate::core;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  vector<uint32_t> used_range_start{&memory_resource_work};
  vector<uint32_t> used_range_end{&memory_resource_work};
  auto offset = RetainMemory(4, 8, &used_range_start, &used_range_end);
  CHECK(offset == 0);
  CHECK(used_range_start.size() == 1);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_end.size() == 1);
  CHECK(used_range_end[0] == 4);
  offset = RetainMemory(4, 8, &used_range_start, &used_range_end);
  CHECK(offset == 8);
  CHECK(used_range_start.size() == 2);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_start[1] == 8);
  CHECK(used_range_end.size() == 2);
  CHECK(used_range_end[0] == 4);
  CHECK(used_range_end[1] == 12);
  offset = RetainMemory(5, 4, &used_range_start, &used_range_end);
  CHECK(offset == 12);
  CHECK(used_range_start.size() == 3);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_start[1] == 8);
  CHECK(used_range_start[2] == 12);
  CHECK(used_range_end.size() == 3);
  CHECK(used_range_end[0] == 4);
  CHECK(used_range_end[1] == 12);
  CHECK(used_range_end[2] == 17);
  ReturnMemory(8, &used_range_start, &used_range_end);
  CHECK(used_range_start.size() == 2);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_start[1] == 12);
  CHECK(used_range_end.size() == 2);
  CHECK(used_range_end[0] == 4);
  CHECK(used_range_end[1] == 17);
  offset = RetainMemory(2, 8, &used_range_start, &used_range_end);
  CHECK(offset == 8);
  CHECK(used_range_start.size() == 3);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_start[1] == 8);
  CHECK(used_range_start[2] == 12);
  CHECK(used_range_end.size() == 3);
  CHECK(used_range_end[0] == 4);
  CHECK(used_range_end[1] == 10);
  CHECK(used_range_end[2] == 17);
  ReturnMemory(8, &used_range_start, &used_range_end);
  CHECK(used_range_start.size() == 2);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_start[1] == 12);
  CHECK(used_range_end.size() == 2);
  CHECK(used_range_end[0] == 4);
  CHECK(used_range_end[1] == 17);
  ReturnMemory(0, &used_range_start, &used_range_end);
  CHECK(used_range_start.size() == 1);
  CHECK(used_range_start[0] == 12);
  CHECK(used_range_end.size() == 1);
  CHECK(used_range_end[0] == 17);
  offset = RetainMemory(4, 8, &used_range_start, &used_range_end);
  CHECK(offset == 0);
  CHECK(used_range_start.size() == 2);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_start[1] == 12);
  CHECK(used_range_end.size() == 2);
  CHECK(used_range_end[0] == 4);
  CHECK(used_range_end[1] == 17);
  offset = RetainMemory(8, 4, &used_range_start, &used_range_end);
  CHECK(used_range_start.size() == 3);
  CHECK(used_range_start[0] == 0);
  CHECK(used_range_start[1] == 4);
  CHECK(used_range_start[2] == 12);
  CHECK(used_range_end.size() == 3);
  CHECK(used_range_end[0] == 4);
  CHECK(used_range_end[1] == 12);
  CHECK(used_range_end[2] == 17);
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
// TODO pass culling
// TODO implement function to flatten buffer_user_pass_list
