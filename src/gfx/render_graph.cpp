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
  if (state & kBufferStateFlagDsvWrite) return kCommandQueueTypeFlagsGraphics;
  if (state & kBufferStateFlagDsvRead) return kCommandQueueTypeFlagsGraphics;
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
RenderGraph::RenderGraph(RenderGraphConfig&& config, std::pmr::memory_resource* memory_resource, std::pmr::memory_resource* memory_resource_work)
    : memory_resource_(memory_resource)
    , render_pass_num_(config.GetRenderPassNum())
{
  vector<vector<BufferStateFlags>> render_pass_buffer_state_flag_list{memory_resource_work};
  std::tie(buffer_id_list_, render_pass_buffer_id_list_, render_pass_buffer_state_flag_list) = InitBufferIdList(render_pass_num_, config.GetRenderPassBufferStateList(), memory_resource_);
  auto [buffer_state_list, buffer_user_pass_list] = CreateBufferStateList(render_pass_num_, buffer_id_list_, render_pass_buffer_id_list_, render_pass_buffer_state_flag_list, memory_resource_work);
  auto initial_state_flag_list = ConvertBufferNameToBufferIdForInitialFlagList(render_pass_num_, config.GetRenderPassBufferStateList(), render_pass_buffer_id_list_, config.GetBufferInitialStateList(), memory_resource_work, memory_resource_work);
  std::tie(buffer_state_list, buffer_user_pass_list) = MergeInitialBufferState(initial_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
  auto final_state_flag_list = ConvertBufferNameToBufferIdForFinalFlagList(render_pass_num_, config.GetRenderPassBufferStateList(), render_pass_buffer_id_list_, config.GetBufferFinalStateList(), memory_resource_work, memory_resource_work);
  std::tie(buffer_state_list, buffer_user_pass_list) = MergeFinalBufferState(final_state_flag_list, std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
  std::tie(buffer_state_list, buffer_user_pass_list) = RevertBufferStateToInitialState(std::move(buffer_state_list), std::move(buffer_user_pass_list), memory_resource_work);
  auto render_pass_command_queue_type_list = config.GetRenderPassCommandQueueTypeList();
  auto node_distance_map = CreateNodeDistanceMapInSameCommandQueueType(render_pass_num_, render_pass_command_queue_type_list, memory_resource_work, memory_resource_work);
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  inter_queue_pass_dependency_ = ConfigureInterQueuePassDependency(buffer_user_pass_list, render_pass_command_queue_type_list, memory_resource, memory_resource_work);
  inter_queue_pass_dependency_ = RemoveRedundantDependencyFromSameQueuePredecessors(render_pass_num_, render_pass_command_queue_type_list, std::move(inter_queue_pass_dependency_), memory_resource_work);
  node_distance_map = AppendInterQueueNodeDistanceMap(inter_queue_pass_dependency_, std::move(node_distance_map));
  node_distance_map = AddNodeDistanceInReverseOrder(std::move(node_distance_map));
  auto render_pass_command_queue_type_flag_list = ConvertToCommandQueueTypeFlagsList(render_pass_command_queue_type_list, memory_resource_work);
  buffer_state_change_info_list_map_ = CreateBufferStateChangeInfoList(render_pass_command_queue_type_flag_list, node_distance_map, buffer_state_list, buffer_user_pass_list, memory_resource);
  render_pass_command_queue_type_list_ = std::move(render_pass_command_queue_type_list);
}
std::tuple<BarrierListPerPass, BarrierListPerPass> ConfigureBarriers(const RenderGraph& render_graph, std::pmr::memory_resource* memory_resource_barriers) {
  auto render_pass_num = render_graph.GetRenderPassNum();
  BarrierListPerPass barriers_pre_pass{memory_resource_barriers};
  barriers_pre_pass.resize(render_pass_num);
  BarrierListPerPass barriers_post_pass{memory_resource_barriers};
  barriers_post_pass.resize(render_pass_num);
  auto& buffer_state_change_info_list_map = render_graph.GetBufferStateChangeInfoListMap();
  auto& buffer_id_list = render_graph.GetBufferIdList();
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
unordered_map<uint32_t, unordered_set<uint32_t>> ConfigureQueueSignals(const RenderGraph& render_graph, std::pmr::memory_resource* memory_resource_signals, std::pmr::memory_resource* memory_resource_work) {
  unordered_map<uint32_t, unordered_set<uint32_t>> queue_signal{memory_resource_signals};
  auto& inter_queue_pass_dependency = render_graph.GetInterQueuePassDependency();
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
  auto pass_num = render_graph.GetRenderPassNum();
  auto& render_pass_command_queue_type_list = render_graph.GetRenderPassCommandQueueTypeList();
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
TEST_CASE("graph node test / find descendant") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  vector<CommandQueueTypeFlags> render_pass_command_queue_type_list;
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  CommandQueueTypeFlags valid_queues{kCommandQueueTypeFlagsAll};
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 0);
  ancestors.push_back(1);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  ancestors.push_back(2);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 2);
  ancestors.clear();
  ancestors.push_back(1);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  ancestors.clear();
  ancestors.push_back(2);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 2);
  ancestors.clear();
  ancestors.push_back(0);
  render_pass_command_queue_type_list[0] = kCommandQueueTypeFlagsCompute;
  valid_queues = kCommandQueueTypeFlagsGraphics;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  node_distance_map.insert_or_assign(3, unordered_map<uint32_t, int32_t>{});
  node_distance_map.at(3).insert_or_assign(3, 0);
  node_distance_map.at(3).insert_or_assign(1, 1);
  node_distance_map.at(3).insert_or_assign(2, 2);
  node_distance_map.at(1).insert_or_assign(3, -1);
  node_distance_map.at(2).insert_or_assign(3, -2);
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  ancestors.clear();
  ancestors.push_back(0);
  ancestors.push_back(3);
  valid_queues = kCommandQueueTypeFlagsAll;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  valid_queues = kCommandQueueTypeFlagsGraphics;
  render_pass_command_queue_type_list[1] = kCommandQueueTypeFlagsCompute;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 2);
  ancestors.clear();
  ancestors.push_back(0);
  ancestors.push_back(1);
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 2);
  ancestors.clear();
  valid_queues = kCommandQueueTypeFlagsAll;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 0);
  ancestors.clear();
  valid_queues = kCommandQueueTypeFlagsGraphics;
  CHECK(FindClosestCommonDescendant(ancestors, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 3);
}
TEST_CASE("graph node test / find ancestor") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  vector<CommandQueueTypeFlags> render_pass_command_queue_type_list;
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  CommandQueueTypeFlags valid_queues{kCommandQueueTypeFlagsAll};
  vector<uint32_t> descendants;
  descendants.push_back(2);
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 2);
  descendants.push_back(1);
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  descendants.push_back(0);
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 0);
  descendants.clear();
  descendants.push_back(0);
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 0);
  descendants.clear();
  descendants.push_back(1);
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  descendants.clear();
  descendants.push_back(2);
  render_pass_command_queue_type_list[2] = kCommandQueueTypeFlagsCompute;
  valid_queues = kCommandQueueTypeFlagsGraphics;
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  node_distance_map.insert_or_assign(3, unordered_map<uint32_t, int32_t>{});
  render_pass_command_queue_type_list.push_back(kCommandQueueTypeFlagsGraphics);
  node_distance_map.at(3).insert_or_assign(3, 0);
  node_distance_map.at(3).insert_or_assign(0, -2);
  node_distance_map.at(3).insert_or_assign(1, -1);
  node_distance_map.at(0).insert_or_assign(3, 2);
  node_distance_map.at(1).insert_or_assign(3, 1);
  descendants.clear();
  descendants.push_back(2);
  descendants.push_back(3);
  valid_queues = kCommandQueueTypeFlagsAll;
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 1);
  valid_queues = kCommandQueueTypeFlagsGraphics;
  render_pass_command_queue_type_list[1] = kCommandQueueTypeFlagsCompute;
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 0);
  descendants.clear();
  descendants.push_back(2);
  descendants.push_back(1);
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 0);
  descendants.clear();
  valid_queues = kCommandQueueTypeFlagsAll;
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 2);
  descendants.clear();
  valid_queues = kCommandQueueTypeFlagsGraphics;
  CHECK(FindClosestCommonAncestor(descendants, node_distance_map, render_pass_command_queue_type_list, valid_queues) == 3);
}
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
    auto& buffer_state_change_info_list = render_graph.GetBufferStateChangeInfoListMap().at(0);
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
    auto& buffer_state_change_info_list = render_graph.GetBufferStateChangeInfoListMap().at(1);
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
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  CHECK(barriers_postpass[1].size() == 2);
  CHECK(barriers_postpass[1][0].buffer_id == 0);
  CHECK(barriers_postpass[1][0].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierUav>(barriers_postpass[1][0].params));
  CHECK(barriers_postpass[1][1].buffer_id == 1);
  CHECK(barriers_postpass[1][1].split_type == BarrierSplitType::kNone);
  CHECK(std::holds_alternative<BarrierTransition>(barriers_postpass[1][1].params));
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_before == kBufferStateFlagRtv);
  CHECK(std::get<BarrierTransition>(barriers_postpass[1][1].params).state_after  == kBufferStateFlagPresent);
  auto queue_signals = ConfigureQueueSignals(render_graph, &memory_resource_work, &memory_resource_work);
  CHECK(queue_signals.size() == 2);
  CHECK(queue_signals.contains(0));
  CHECK(queue_signals.at(0).size() == 1);
  CHECK(queue_signals.at(0).contains(1));
  CHECK(queue_signals.contains(1));
  CHECK(queue_signals.at(1).size() == 1);
  CHECK(queue_signals.at(1).contains(0));
}
#if 0
TEST_CASE("barrier for load from srv") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
