#include "render_graph.h"
namespace illuminate::gfx {
std::tuple<RenderPassIdMap, RenderPassOrder> FormatRenderPassList(RenderPassList&& render_pass_list, std::pmr::memory_resource* memory_resource) {
  RenderPassIdMap render_pass_id_map{memory_resource};
  render_pass_id_map.reserve(render_pass_list.size());
  RenderPassOrder render_pass_order{memory_resource};
  render_pass_order.reserve(render_pass_list.size());
  for (auto&& pass : render_pass_list) {
    render_pass_order.push_back(pass.name);
    render_pass_id_map.insert({render_pass_order.back(), std::move(pass)});
  }
  return {render_pass_id_map, render_pass_order};
}
BufferIdList CreateBufferIdList(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, std::pmr::memory_resource* memory_resource) {
  BufferIdList buffer_id_list{memory_resource};
  buffer_id_list.reserve(render_pass_order.size());
  BufferId new_id = 0;
  std::pmr::unordered_map<StrId, BufferId> known_buffer{memory_resource};
  for (auto& pass_name : render_pass_order) {
    auto& pass_buffer_ids = buffer_id_list.insert({pass_name, PassBufferIdList{memory_resource}}).first->second;
    auto& pass = render_pass_id_map.at(pass_name);
    pass_buffer_ids.reserve(pass.buffer_list.size());
    for (auto& buffer : pass.buffer_list) {
      if (buffer.load_op_type == BufferLoadOpType::kDontCare || buffer.load_op_type == BufferLoadOpType::kClear || !known_buffer.contains(buffer.name)) {
        pass_buffer_ids.push_back(new_id);
        known_buffer.insert({buffer.name, new_id});
        new_id++;
      } else {
        pass_buffer_ids.push_back(known_buffer.at(buffer.name));
      }
    }
  }
  return buffer_id_list;
}
BufferIdList ApplyBufferNameAlias(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, BufferIdList&& buffer_id_list, const BufferNameAliasList& alias_list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<StrId, BufferId> buffer_name_to_id(memory_resource);
  for (auto& pass_name : render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
      for (auto& [buffer_name, alias_name] : alias_list) {
        if (buffer_name == buffer.name) {
          buffer_name_to_id[alias_name] = buffer_id_list.at(pass.name)[buffer_index];
        } else if (alias_name == buffer.name) {
          buffer_id_list.at(pass.name)[buffer_index] = buffer_name_to_id.at(alias_name);
        }
        buffer_index++;
      }
    }
  }
  return std::move(buffer_id_list);
}
RenderPassAdjacencyGraph CreateRenderPassAdjacencyGraph(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, std::pmr::memory_resource* memory_resource) {
  RenderPassAdjacencyGraph adjacency_graph(memory_resource);
  for (auto& pass_name : render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    auto& pass_buffer_ids = buffer_id_list.at(pass_name);
    for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
      if (IsOutputBuffer(buffer.state_type, buffer.load_op_type)) {
        if (!adjacency_graph.output_buffer_producer_pass.contains(pass_buffer_ids[buffer_index])) {
          adjacency_graph.output_buffer_producer_pass.insert({pass_buffer_ids[buffer_index], std::pmr::vector<StrId>(memory_resource)});
        }
        adjacency_graph.output_buffer_producer_pass.at(pass_buffer_ids[buffer_index]).push_back(pass_name);
      }
      if (IsInitialValueUsed(buffer.state_type, buffer.load_op_type)) {
        if (!adjacency_graph.consumer_pass_input_buffer.contains(pass_name)) {
          adjacency_graph.consumer_pass_input_buffer.insert({pass_name, std::pmr::vector<BufferId>(memory_resource)});
        }
        adjacency_graph.consumer_pass_input_buffer[pass_name].push_back(pass_buffer_ids[buffer_index]);
      }
      buffer_index++;
    }
  }
  return adjacency_graph;
}
MandatoryOutputBufferIdList IdentifyMandatoryOutputBufferId(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, const MandatoryOutputBufferNameList& mandatory_buffer_name_list, std::pmr::memory_resource* memory_resource) {
  MandatoryOutputBufferIdList mandatory_buffer_id_list(memory_resource);
  mandatory_buffer_id_list.reserve(mandatory_buffer_name_list.size());
  for (auto& buffer_name : mandatory_buffer_name_list) {
    for (auto it = render_pass_order.crbegin(); it != render_pass_order.crend(); it++) {
      bool buffer_found = false;
      auto& pass_name = *it;
      auto& pass = render_pass_id_map.at(pass_name);
      for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
        if (buffer.name == buffer_name && IsOutputBuffer(buffer.state_type, buffer.load_op_type)) {
          mandatory_buffer_id_list.insert({buffer.name, buffer_id_list.at(pass_name)[buffer_index]});
          buffer_found = true;
          break;
        }
        buffer_index++;
      }
      if (buffer_found) break;
    }
  }
  return mandatory_buffer_id_list;
}
std::pmr::unordered_set<StrId> GetBufferProducerPassList(const RenderPassAdjacencyGraph& adjacency_graph, std::pmr::unordered_set<BufferId>&& buffer_id_list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<StrId> buffer_producer_pass_list{memory_resource};
  auto buffer_id_it = buffer_id_list.begin();
  while (buffer_id_it != buffer_id_list.end()) {
    auto& producer_pass = adjacency_graph.output_buffer_producer_pass.at(*buffer_id_it);
    buffer_producer_pass_list.insert(producer_pass.begin(), producer_pass.end());
    buffer_id_it = buffer_id_list.erase(buffer_id_it);
  }
  return buffer_producer_pass_list;
}
ConsumerProducerRenderPassMap CreateConsumerProducerMap(const RenderPassAdjacencyGraph& adjacency_graph, std::pmr::memory_resource* memory_resource) {
  ConsumerProducerRenderPassMap consumer_producer_render_pass_map{memory_resource};
  consumer_producer_render_pass_map.reserve(adjacency_graph.consumer_pass_input_buffer.size());
  for (auto& [consumer_pass, input_buffers] : adjacency_graph.consumer_pass_input_buffer) {
    consumer_producer_render_pass_map.insert({consumer_pass, std::pmr::unordered_set<StrId>{memory_resource}});
    for (auto& buffer_id : input_buffers) {
      if (!adjacency_graph.output_buffer_producer_pass.contains(buffer_id)) continue;
      auto& producer_pass = adjacency_graph.output_buffer_producer_pass.at(buffer_id);
      consumer_producer_render_pass_map.at(consumer_pass).insert(producer_pass.begin(), producer_pass.end());
    }
  }
  return consumer_producer_render_pass_map;
}
std::pmr::unordered_set<StrId> GetUsedRenderPassList(std::pmr::unordered_set<StrId>&& used_pass, const ConsumerProducerRenderPassMap& consumer_producer_render_pass_map) {
  std::pmr::unordered_set<StrId> pass_to_check = used_pass;
  while (!pass_to_check.empty()) {
    auto consumer_pass = *pass_to_check.begin();
    pass_to_check.erase(consumer_pass);
    if (!consumer_producer_render_pass_map.contains(consumer_pass)) continue;
    auto& producer_pass_list = consumer_producer_render_pass_map.at(consumer_pass);
    for (auto& producer_pass : producer_pass_list) {
      if (used_pass.contains(producer_pass)) continue;
      used_pass.insert(producer_pass);
      pass_to_check.insert(producer_pass);
    }
  }
  return std::move(used_pass);
}
RenderPassOrder CullUnusedRenderPass(RenderPassOrder&& render_pass_order, const std::pmr::unordered_set<StrId>& used_render_pass_list, const RenderPassIdMap& render_pass_id_map) {
  std::erase_if(render_pass_order, [&used_render_pass_list, &render_pass_id_map](const StrId& pass_name) { return !render_pass_id_map.at(pass_name).mandatory_pass && !used_render_pass_list.contains(pass_name); });
  return std::move(render_pass_order);
}
BufferIdList RemoveUnusedBuffers(const RenderPassOrder& render_pass_order, BufferIdList&& buffer_id_list) {
  auto it = buffer_id_list.begin();
  while (it != buffer_id_list.end()) {
    if (IsContaining(render_pass_order, it->first)) {
      it++;
    } else {
      it = buffer_id_list.erase(it);
    }
  }
  return std::move(buffer_id_list);
}
bool IsDuplicateRenderPassNameExists(const RenderPassList& list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<StrId> names(memory_resource);
  for (auto& pass : list) {
    if (names.contains(pass.name)) return true;
    names.insert(pass.name);
  }
  return false;
}
BufferCreationDescList ConfigureBufferCreationDescs(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, const BufferSize2d& mainbuffer_size, const BufferSize2d& swapchain_size, std::pmr::memory_resource* memory_resource) {
  BufferCreationDescList buffer_creation_descs{memory_resource};
  for (auto& pass_name : render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    auto& pass_buffer_ids = buffer_id_list.at(pass_name);
    for (uint32_t buffer_index = 0; auto& buffer : pass.buffer_list) {
      auto buffer_id = pass_buffer_ids[buffer_index];
      if (!buffer_creation_descs.contains(buffer_id)) {
        buffer_creation_descs.insert({buffer_id, BufferCreationDesc(buffer, mainbuffer_size, swapchain_size)});
      }
      auto& desc = buffer_creation_descs.at(buffer_id);
      auto new_flag = GetBufferStateFlag(buffer.state_type, buffer.load_op_type);
      if (IsBufferStateFlagMergeable(desc.initial_state_flag, new_flag)) {
        desc.initial_state_flag = static_cast<BufferStateFlags>(desc.initial_state_flag | new_flag);
      }
      desc.state_flags = static_cast<BufferStateFlags>(new_flag | desc.state_flags);
      buffer_index++;
    }
  }
  return buffer_creation_descs;
}
std::tuple<std::pmr::unordered_map<BufferId, uint32_t>, std::pmr::unordered_map<BufferId, uint32_t>> GetPhysicalBufferSizes(const BufferCreationDescList& buffer_creation_descs, std::function<std::tuple<uint32_t, uint32_t>(const BufferCreationDesc&)>&& buffer_creation_func, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_size_in_byte{memory_resource};
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_alignment{memory_resource};
  for (auto& [id, desc] : buffer_creation_descs) {
    std::tie(physical_buffer_size_in_byte[id], physical_buffer_alignment[id]) = buffer_creation_func(desc);
  }
  return {physical_buffer_size_in_byte, physical_buffer_alignment};
}
std::tuple<std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>, std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>> CalculatePhysicalBufferLiftime(const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>> physical_buffer_lifetime_begin_pass{memory_resource};
  std::pmr::unordered_set<BufferId> processed_buffer;
  for (auto& pass_name : render_pass_order) {
    auto& pass_buffer_ids = buffer_id_list.at(pass_name);
    auto [it, result] = physical_buffer_lifetime_begin_pass.insert({pass_name, std::pmr::vector<BufferId>{memory_resource}});
    for (auto& buffer_id : pass_buffer_ids) {
      if (!processed_buffer.contains(buffer_id)) {
        processed_buffer.insert(buffer_id);
        it->second.push_back(buffer_id);
      }
    }
  }
  processed_buffer.clear();
  std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>> physical_buffer_lifetime_end_pass{memory_resource};
  for (auto pass_it = render_pass_order.crbegin(); pass_it != render_pass_order.crend(); pass_it++) {
    auto& pass_name = *pass_it;
    auto& pass_buffer_ids = buffer_id_list.at(pass_name);
    auto [it, result] = physical_buffer_lifetime_end_pass.insert({pass_name, std::pmr::vector<BufferId>{memory_resource}});
    for (auto& buffer_id : pass_buffer_ids) {
      if (!processed_buffer.contains(buffer_id)) {
        processed_buffer.insert(buffer_id);
        it->second.push_back(buffer_id);
      }
    }
  }
  return {physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass};
}
namespace {
struct Address { uint32_t head_address, size_in_bytes; };
void MergeToFreeMemory(const uint32_t addr, const uint32_t& size_in_bytes, std::pmr::vector<Address> * const free_memory) {
  for (auto it = free_memory->begin(); it != free_memory->end(); it++) {
    if (it->head_address + it->size_in_bytes == addr) {
      auto head_address = it->head_address;
      auto new_size = it->size_in_bytes + size_in_bytes;
      free_memory->erase(it);
      MergeToFreeMemory(head_address, new_size, free_memory);
      return;
    }
    if (addr + size_in_bytes == it->head_address) {
      auto head_address = addr;
      auto new_size = size_in_bytes + it->size_in_bytes;
      free_memory->erase(it);
      MergeToFreeMemory(head_address, new_size, free_memory);
      return;
    }
  }
  free_memory->push_back({addr, size_in_bytes});
}
uint32_t AllocateAlignedAddressFromFreeMemory(const uint32_t size_in_bytes, const uint32_t alignment, std::pmr::vector<Address> * const free_memory) {
  for (auto it = free_memory->begin(); it != free_memory->end(); it++) {
    if (it->size_in_bytes < size_in_bytes) continue;
    auto aligned_addr = static_cast<uint32_t>(illuminate::core::AlignAddress(it->head_address, alignment));
    auto alignment_offset = aligned_addr - it->head_address;
    auto memory_left = it->size_in_bytes - alignment_offset;
    if (memory_left < size_in_bytes) continue;
    auto original_head = it->head_address;
    free_memory->erase(it);
    if (alignment_offset > 0) {
      free_memory->push_back({original_head, alignment_offset});
    }
    if (memory_left > size_in_bytes) {
      free_memory->push_back({aligned_addr + size_in_bytes, memory_left - size_in_bytes});
    }
    return aligned_addr;
  }
  return std::numeric_limits<uint32_t>::max();
}
}
std::pmr::unordered_map<BufferId, uint32_t> GetPhysicalBufferAddressOffset(const RenderPassOrder& render_pass_order, const std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>& physical_buffer_lifetime_begin_pass, const std::pmr::unordered_map<StrId, std::pmr::vector<BufferId>>& physical_buffer_lifetime_end_pass, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_size_in_byte, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_alignment, std::pmr::memory_resource* memory_resource) {
  // TODO use better tactics with real performance measured.
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_address_offset{memory_resource};
  std::pmr::vector<Address> free_memory{{{0, std::numeric_limits<uint32_t>::max()}}, memory_resource};
  for (auto& pass_name : render_pass_order) {
    for (auto& buffer_id : physical_buffer_lifetime_begin_pass.at(pass_name)) {
      if (physical_buffer_address_offset.contains(buffer_id)) continue;
      auto aligned_addr = AllocateAlignedAddressFromFreeMemory(physical_buffer_size_in_byte.at(buffer_id), physical_buffer_alignment.at(buffer_id), &free_memory);
      physical_buffer_address_offset.insert({buffer_id, aligned_addr});
    }
    for (auto& buffer_id : physical_buffer_lifetime_end_pass.at(pass_name)) {
      MergeToFreeMemory(physical_buffer_address_offset.at(buffer_id), physical_buffer_size_in_byte.at(buffer_id), &free_memory);
    }
  }
  return physical_buffer_address_offset;
}
std::tuple<BatchInfoList, RenderPassOrder> ConfigureAsyncComputeBatching(const RenderPassIdMap& render_pass_id_map, RenderPassOrder&& current_render_pass_order, RenderPassOrder&& prev_render_pass_order, const AsyncComputePairInfo& async_group_info, std::pmr::memory_resource* memory_resource) {
  BatchInfoList batch_info{memory_resource};
  batch_info.push_back(std::pmr::vector<StrId>{memory_resource});
  RenderPassOrder render_pass_unprocessed{memory_resource};
  std::pmr::unordered_map<StrId, uint32_t> async_group_batch_index{memory_resource};
  auto pass_name_it = prev_render_pass_order.begin();
  while (pass_name_it != prev_render_pass_order.end()) {
    auto&& current_pass_name = std::move(*pass_name_it);
    auto& pass = render_pass_id_map.at(current_pass_name);
    auto batch_index = batch_info.size() - 1;
    for (auto& [group_name, pair_type] : async_group_info) {
      if (group_name != pass.async_compute_group) continue;
      if (pair_type == AsyncComputeBatchPairType::kPairComputeWithNextFrameGraphics && pass.command_queue_type == CommandQueueType::kCompute) {
        if (!async_group_batch_index.contains(group_name)) {
          if (!batch_info.back().empty()) {
            batch_info.push_back(std::pmr::vector<StrId>{memory_resource});
          }
          async_group_batch_index.insert({group_name, batch_info.size() - 1});
        }
        batch_index = async_group_batch_index.at(group_name);
      }
      break;
    }
    batch_info[batch_index].push_back(std::move(current_pass_name));
    pass_name_it = prev_render_pass_order.erase(pass_name_it);
  }
  pass_name_it = current_render_pass_order.begin();
  while (pass_name_it != current_render_pass_order.end()) {
    auto&& current_pass_name = std::move(*pass_name_it);
    auto& pass = render_pass_id_map.at(current_pass_name);
    auto batch_index = batch_info.size() - 1;
    for (auto& [group_name, pair_type] : async_group_info) {
      if (group_name != pass.async_compute_group) {
        if (render_pass_unprocessed.empty()) continue;
        if (pass.command_queue_type != CommandQueueType::kCompute) continue;
        batch_index = ~0u;
        break;
      }
      if (pair_type == AsyncComputeBatchPairType::kPairComputeWithNextFrameGraphics && pass.command_queue_type == CommandQueueType::kCompute) {
        batch_index = ~0u;
        break;
      }
      if (!async_group_batch_index.contains(group_name)) {
        if (!batch_info.back().empty()) {
          batch_info.push_back(std::pmr::vector<StrId>{memory_resource});
        }
        async_group_batch_index.insert({group_name, batch_info.size() - 1});
      }
      batch_index = async_group_batch_index.at(group_name);
      break;
    }
    if (batch_index == ~0u) {
      render_pass_unprocessed.push_back(std::move(current_pass_name));
    } else {
      batch_info[batch_index].push_back(std::move(current_pass_name));
    }
    pass_name_it = current_render_pass_order.erase(pass_name_it);
  }
  return {batch_info, render_pass_unprocessed};
}
PassSignalInfo ConfigureBufferResourceDependency(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const ConsumerProducerRenderPassMap& consumer_producer_render_pass_map, std::pmr::memory_resource* memory_resource) {
  PassSignalInfo pass_signal_info{memory_resource};
  for (auto& consumer_pass_name : render_pass_order) {
    if (!consumer_producer_render_pass_map.contains(consumer_pass_name)) continue;
    for (auto& producer_pass_name : consumer_producer_render_pass_map.at(consumer_pass_name)) {
      auto consumer_command_queue_type = render_pass_id_map.at(consumer_pass_name).command_queue_type;
      auto producer_command_queue_type = render_pass_id_map.at(producer_pass_name).command_queue_type;
      if (producer_command_queue_type == consumer_command_queue_type) continue;
      if (!pass_signal_info.contains(producer_pass_name)) {
        pass_signal_info.insert({producer_pass_name, std::pmr::unordered_set<StrId>{memory_resource}});
      }
      pass_signal_info.at(producer_pass_name).insert(consumer_pass_name);
    }
  }
  return pass_signal_info;
}
PassSignalInfo ConvertBatchToSignalInfo(const BatchInfoList& batch_info_list, const RenderPassIdMap& render_pass_id_map, std::pmr::memory_resource* memory_resource) {
  PassSignalInfo pass_signal_wait_info{memory_resource};
  std::pmr::unordered_map<CommandQueueType, StrId> last_pass_executed_per_batch{memory_resource};
  CommandQueueTypeFlag pass_executed_queue = kCommandQueueTypeNone;
  for (uint32_t i = 0; i < batch_info_list.size() - 1; i++) {
    for (auto& pass_name : batch_info_list[i]) {
      auto& pass = render_pass_id_map.at(pass_name);
      last_pass_executed_per_batch.insert_or_assign(pass.command_queue_type, pass_name);
    }
    for (auto& pass_name : batch_info_list[i + 1]) {
      auto& pass = render_pass_id_map.at(pass_name);
      if (IsContainingCommandQueueType(pass_executed_queue, pass.command_queue_type)) continue;
      MergeCommandQueueTypeFlag(&pass_executed_queue, pass.command_queue_type);
      for (auto& [signal_queue, signal_pass] : last_pass_executed_per_batch) {
        if (signal_queue == pass.command_queue_type) continue;
        if (!pass_signal_wait_info.contains(signal_pass)) {
          pass_signal_wait_info.insert({signal_pass, std::pmr::unordered_set<StrId>{memory_resource}});
        }
        pass_signal_wait_info.at(signal_pass).insert(pass_name);
      }
    }
    last_pass_executed_per_batch.clear();
    pass_executed_queue = kCommandQueueTypeNone;
  }
  return pass_signal_wait_info;
}
PassSignalInfo MergePassSignalInfo(PassSignalInfo&& a, PassSignalInfo&& b) {
  auto&& dst = std::move(a);
  while (!b.empty()) {
    dst[b.begin()->first].insert(std::make_move_iterator(b.begin()->second.begin()), std::make_move_iterator(b.begin()->second.end()));
    b.erase(b.begin());
  }
  return std::move(dst);
}
PassSignalInfo RemoveRedundantPassSignalInfo(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, PassSignalInfo&& pass_signal_info, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<StrId, std::pmr::unordered_map<CommandQueueType, std::pmr::unordered_set<StrId>>> waiting_pass{memory_resource};
  std::pmr::unordered_map<StrId, uint32_t> pass_index{memory_resource};
  std::pmr::unordered_map<CommandQueueType, std::pmr::unordered_map<CommandQueueType, uint32_t>> max_processed_signal{memory_resource};
  for (uint32_t i = 0; i < render_pass_order.size(); i++) {
    auto& signal_pass = render_pass_order[i];
    pass_index.insert({signal_pass, i});
    if (pass_signal_info.contains(signal_pass)) {
      auto& signal_queue = render_pass_id_map.at(signal_pass).command_queue_type;
      for (auto& wait_pass : pass_signal_info.at(signal_pass)) {
        auto& wait_queue = render_pass_id_map.at(wait_pass).command_queue_type;
        if (wait_queue == signal_queue) continue;
        if (!waiting_pass.contains(wait_pass)) {
          waiting_pass.insert({wait_pass, std::pmr::unordered_map<CommandQueueType, std::pmr::unordered_set<StrId>>{memory_resource}});
        }
        if (!waiting_pass.at(wait_pass).contains(signal_queue)) {
          waiting_pass.at(wait_pass).insert({signal_queue, std::pmr::unordered_set<StrId>{memory_resource}});
        }
        waiting_pass.at(wait_pass).at(signal_queue).insert(signal_pass);
      }
    }
    auto& wait_pass = signal_pass;
    if (!waiting_pass.contains(wait_pass)) continue;
    auto& wait_queue = render_pass_id_map.at(wait_pass).command_queue_type;
    for (auto& [signal_queue, signal_pass_list] : waiting_pass.at(wait_pass)) {
      if (wait_queue == signal_queue) continue;
      for (auto& signal_pass : signal_pass_list) {
        auto& signal_pass_index = pass_index.at(signal_pass);
        if (max_processed_signal.contains(wait_queue) && max_processed_signal.at(wait_queue).contains(signal_queue) && max_processed_signal.at(wait_queue).at(signal_queue) >= signal_pass_index) {
          pass_signal_info.at(signal_pass).erase(wait_pass);
          if (pass_signal_info.at(signal_pass).empty()) {
            pass_signal_info.erase(signal_pass);
          }
          continue;
        }
        if (!max_processed_signal.contains(wait_queue)) {
          max_processed_signal.insert({wait_queue, std::pmr::unordered_map<CommandQueueType, uint32_t>{memory_resource}});
        }
        max_processed_signal.at(wait_queue).insert_or_assign(signal_queue, signal_pass_index);
      }
    }
  }
  return std::move(pass_signal_info);
}
RenderPassOrder ConvertBatchInfoBackToRenderPassOrder(BatchInfoList&& batch_info_list, std::pmr::memory_resource* memory_resource) {
  RenderPassOrder render_pass_order{memory_resource};
  while (!batch_info_list.empty()) {
    auto&& batch = std::move(*batch_info_list.begin());
    while (!batch.empty()) {
      render_pass_order.push_back(std::move(*batch.begin()));
      batch.erase(batch.begin());
    }
    batch_info_list.erase(batch_info_list.begin());
  }
  return render_pass_order;
}
BufferStateList CreateBufferCreationStateList(const BufferCreationDescList& buffer_creation_descs, std::pmr::memory_resource* memory_resource) {
  BufferStateList buffer_creation_state_list{memory_resource};
  for (auto& [buffer_id, desc] : buffer_creation_descs) {
    buffer_creation_state_list.insert({buffer_id, desc.initial_state_flag});
  }
  return buffer_creation_state_list;
}
constexpr CommandQueueTypeFlag GetValidCommandQueues(const BufferStateFlags& flag) {
  if (flag & kBufferStateFlagNone) return kCommandQueueTypeNone;
  if (flag & kBufferStateFlagSrvPsOnly) return kCommandQueueTypeGraphics;
  if (flag & kBufferStateFlagRtv) return kCommandQueueTypeGraphics;
  if (flag & kBufferStateFlagDsvWrite) return kCommandQueueTypeGraphics;
  if (flag & kBufferStateFlagDsvRead) return kCommandQueueTypeGraphics;
  if (!(flag & ~(kBufferStateFlagCopySrc | kBufferStateFlagCopyDst))) return kCommandQueueTypeAll;
  return kCommandQueueTypeGC;
}
namespace {
std::pair<StrId, StrId> GetBarrierBeginEndPass(const BufferStateFlags& prev_buffer_state, const BufferStateFlags& next_buffer_state, const std::pmr::unordered_set<StrId>& pass_list_to_access_prev_buffer_state, const std::pmr::unordered_set<StrId>& pass_list_to_access_next_buffer_state, const RenderPassIdMap& render_pass_id_map, const std::pmr::unordered_map<StrId, std::pmr::unordered_map<StrId, int32_t>>& inter_pass_distance_map, std::pmr::memory_resource* memory_resource) {
  auto valid_queues = GetValidCommandQueues(prev_buffer_state) & GetValidCommandQueues(next_buffer_state);
  std::pmr::unordered_set<StrId> common_pass_list{memory_resource};
  {
    std::pmr::unordered_set<StrId> candidate_passes{memory_resource};
    // find common descendents
    for (auto& pass_name : pass_list_to_access_prev_buffer_state) {
      if (!pass_name) continue;
      for (auto& [other_pass, distance] : inter_pass_distance_map.at(pass_name)) {
        if (distance < 0) continue;
        if (!(valid_queues & ConvertCommandQueueTypeToFlag(render_pass_id_map.at(other_pass).command_queue_type))) continue;
        candidate_passes.insert(other_pass);
      }
      if (common_pass_list.empty()) {
        // what if no common pass really existed?
        common_pass_list = std::move(candidate_passes);
      } else {
        auto it = common_pass_list.begin();
        while (it != common_pass_list.end()) {
          if (candidate_passes.contains(*it)) {
            it++;
          } else {
            it = common_pass_list.erase(it);
          }
        }
      }
      candidate_passes.clear();
    }
    if (common_pass_list.empty()) {
      // what if no common pass really existed?
      common_pass_list.reserve(inter_pass_distance_map.size());
      for (auto& entity : inter_pass_distance_map) {
        common_pass_list.insert(entity.first);
      }
    }
    // find common ancestors within common_pass_list
    for (auto& pass_name : pass_list_to_access_next_buffer_state) {
      if (!pass_name) continue;
      for (auto& [other_pass, distance] : inter_pass_distance_map.at(pass_name)) {
        if (distance > 0) continue;
        if (!(valid_queues & ConvertCommandQueueTypeToFlag(render_pass_id_map.at(other_pass).command_queue_type))) continue;
        candidate_passes.insert(other_pass);
      }
      auto it = common_pass_list.begin();
      while (it != common_pass_list.end()) {
        if (candidate_passes.contains(*it)) {
          it++;
        } else {
          it = common_pass_list.erase(it);
        }
      }
      candidate_passes.clear();
    }
  }
  StrId begin_pass, end_pass;
  int32_t largest_distance = 0;
  bool same_queue = false;
  for (auto& pass_name : common_pass_list) {
    for (auto& [other_pass, distance] : inter_pass_distance_map.at(pass_name)) {
      if (!common_pass_list.contains(other_pass)) continue;
      auto abs_distance = std::abs(distance);
      if (abs_distance < largest_distance) continue;
      if (abs_distance == largest_distance) {
        if (same_queue) continue;
        if (begin_pass && end_pass && render_pass_id_map.at(pass_name).command_queue_type != render_pass_id_map.at(other_pass).command_queue_type) continue;
      }
      largest_distance = abs_distance;
      same_queue = render_pass_id_map.at(pass_name).command_queue_type == render_pass_id_map.at(other_pass).command_queue_type;
      if (distance < 0) {
        begin_pass = other_pass;
        end_pass = pass_name;
      } else {
        begin_pass = pass_name;
        end_pass = other_pass;
      }
    }
  }
  return {begin_pass, end_pass};
}
}
BufferStateChangeInfoList GatherBufferStateChangeInfo(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, const BufferStateList& buffer_state_before_render_pass_list, const BufferStateList& buffer_state_after_render_pass_list, std::pmr::memory_resource* memory_resource) {
  // gather state change info per buffer
  BufferStateChangeInfoList buffer_state_change_list{memory_resource};
  std::pmr::unordered_map<BufferId, std::pmr::unordered_map<CommandQueueType, StrId>> last_pass_accessed_per_buffer{memory_resource};
  for (auto& pass_name : render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    auto& buffer_ids = buffer_id_list.at(pass_name);
    for (uint32_t buffer_index = 0; auto& buffer_config : pass.buffer_list) {
      auto& buffer_id = buffer_ids[buffer_index++];
      auto barrier_exists = buffer_state_change_list.contains(buffer_id);
      auto flag = GetBufferStateFlag(buffer_config.state_type, buffer_config.load_op_type);
      auto& last_flag = barrier_exists ? buffer_state_change_list.at(buffer_id).back().next_buffer_state : buffer_state_before_render_pass_list.at(buffer_id);
      if (bool flag_in_last_flag = (flag & last_flag); flag_in_last_flag || IsBufferStateFlagMergeable(flag, last_flag)) {
        if (barrier_exists && !flag_in_last_flag) {
          auto& barrier = buffer_state_change_list.at(buffer_id).back();
          barrier.next_buffer_state = static_cast<decltype(flag)>(flag | last_flag);
        }
      } else {
        if (!barrier_exists) {
          barrier_exists = true;
          buffer_state_change_list.insert({buffer_id, std::pmr::vector<BufferStateChangeInfo>{memory_resource}});
        }
        buffer_state_change_list.at(buffer_id).push_back({last_flag, flag, std::pmr::unordered_set<StrId>{memory_resource}, std::pmr::unordered_set<StrId>{memory_resource}});
        if (last_pass_accessed_per_buffer.contains(buffer_id)) {
          auto& barrier = buffer_state_change_list.at(buffer_id).back();
          for (auto&& [queue, accessed_pass] : last_pass_accessed_per_buffer.at(buffer_id)) {
            barrier.pass_list_to_access_prev_buffer_state.insert(std::move(accessed_pass));
          }
          last_pass_accessed_per_buffer.erase(buffer_id);
        }
      }
      if (barrier_exists) {
        buffer_state_change_list.at(buffer_id).back().pass_list_to_access_next_buffer_state.insert(pass_name);
      }
      if (!last_pass_accessed_per_buffer.contains(buffer_id)) {
        last_pass_accessed_per_buffer.insert({buffer_id, std::pmr::unordered_map<CommandQueueType, StrId>{memory_resource}});
      }
      last_pass_accessed_per_buffer.at(buffer_id).insert_or_assign(pass.command_queue_type, pass_name);
    }
  }
  // add state change info after all render pass is done
  for (auto& [buffer_id, flag] : buffer_state_after_render_pass_list) {
    auto barrier_exists = buffer_state_change_list.contains(buffer_id);
    auto& last_flag = barrier_exists ? buffer_state_change_list.at(buffer_id).back().next_buffer_state : buffer_state_before_render_pass_list.at(buffer_id);
    if (flag & last_flag) continue;
    if (!barrier_exists) {
      buffer_state_change_list.insert({buffer_id, std::pmr::vector<BufferStateChangeInfo>{memory_resource}});
    }
    buffer_state_change_list.at(buffer_id).push_back({last_flag, flag, std::pmr::unordered_set<StrId>{memory_resource}, std::pmr::unordered_set<StrId>{memory_resource}});
    if (last_pass_accessed_per_buffer.contains(buffer_id)) {
      auto& barrier = buffer_state_change_list.at(buffer_id).back();
      for (auto&& [queue, accessed_pass] : last_pass_accessed_per_buffer.at(buffer_id)) {
        barrier.pass_list_to_access_prev_buffer_state.insert(std::move(accessed_pass));
      }
    }
  }
  return buffer_state_change_list;
}
InterPassDistanceMap CreateInterPassDistanceMap(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const PassSignalInfo& pass_signal_info, std::pmr::memory_resource* memory_resource) {
  // create inter pass distance map
  InterPassDistanceMap inter_pass_distance_map{memory_resource};
  std::pmr::unordered_map<StrId, std::pmr::unordered_set<StrId>> wait_signal_info{memory_resource};
  for (auto& [signal_pass, wait_info]: pass_signal_info) {
    for (auto& wait_pass : wait_info) {
      if (!wait_signal_info.contains(wait_pass)) {
        wait_signal_info.insert({wait_pass, std::pmr::unordered_set<StrId>{memory_resource}});
      }
      wait_signal_info.at(wait_pass).insert(signal_pass);
    }
  }
  inter_pass_distance_map.reserve(render_pass_order.size());
  std::pmr::unordered_map<CommandQueueType, StrId> prev_pass_per_command_queue_type{memory_resource};
  for (auto& pass_name : render_pass_order) {
    inter_pass_distance_map.insert({pass_name, std::pmr::unordered_map<StrId, int32_t>{memory_resource}});
    inter_pass_distance_map.at(pass_name).insert({pass_name, 0});
    auto& pass = render_pass_id_map.at(pass_name);
    auto& command_queue_type = pass.command_queue_type;
    if (prev_pass_per_command_queue_type.contains(command_queue_type)) {
      auto& prev_pass = prev_pass_per_command_queue_type.at(command_queue_type);
      if (inter_pass_distance_map.contains(prev_pass)) {
        for (auto& [ancestor, distance] : inter_pass_distance_map.at(prev_pass)) {
          inter_pass_distance_map.at(pass_name).insert({ancestor, distance - 1});
        }
      }
    }
    if (wait_signal_info.contains(pass_name)) {
      for (auto& signal_pass : wait_signal_info.at(pass_name)) {
        for (auto& [ancestor, distance] : inter_pass_distance_map.at(signal_pass)) {
          inter_pass_distance_map.at(pass_name).insert({ancestor, distance - 1});
        }
      }
    }
    prev_pass_per_command_queue_type[command_queue_type] = pass_name;
  }
  for (auto& descendent : render_pass_order) {
    auto& map = inter_pass_distance_map.at(descendent);
    for (auto& [ancestor, distance] : map) {
      if (distance < 0) {
        inter_pass_distance_map.at(ancestor).insert({descendent, -distance});
      }
    }
  }
  return inter_pass_distance_map;
}
PassBarrierInfoSet ConfigureBarrier(const RenderPassIdMap& render_pass_id_map, const BufferStateChangeInfoList& buffer_state_change_list, const InterPassDistanceMap& inter_pass_distance_map, std::pmr::memory_resource* memory_resource) {
  PassBarrierInfo barrier_before_pass{memory_resource};
  PassBarrierInfo barrier_after_pass{memory_resource};
  std::pmr::vector<BarrierConfig> barrier_info_list{memory_resource};
  std::pmr::vector<StrId> barrier_pass_name{memory_resource};
  std::pmr::vector<PassBarrierInfo*> barrier_dst_list_ptr{memory_resource};
  for (auto&& [buffer_id, state_change_info_list] : buffer_state_change_list) {
    for (auto&& state_change_info : state_change_info_list) {
      auto [split_barrier_begin_pass, split_barrier_end_pass] = GetBarrierBeginEndPass(state_change_info.prev_buffer_state, state_change_info.next_buffer_state, state_change_info.pass_list_to_access_prev_buffer_state, state_change_info.pass_list_to_access_next_buffer_state, render_pass_id_map, inter_pass_distance_map, memory_resource);
      if (!split_barrier_begin_pass && !split_barrier_end_pass) {
        logerror("no valid barrier pass found. {}", buffer_id, state_change_info.prev_buffer_state, state_change_info.next_buffer_state);
        continue;
      }
      barrier_pass_name.push_back(split_barrier_begin_pass);
      if (state_change_info.pass_list_to_access_prev_buffer_state.contains(split_barrier_begin_pass)) {
        barrier_dst_list_ptr.push_back(&barrier_after_pass);
      } else {
        barrier_dst_list_ptr.push_back(&barrier_before_pass);
      }
      barrier_info_list.push_back({buffer_id, state_change_info.prev_buffer_state, state_change_info.next_buffer_state, BarrierSplitType::kNone});
      if (!split_barrier_begin_pass) {
        barrier_pass_name.back() = split_barrier_end_pass;
        if (state_change_info.pass_list_to_access_next_buffer_state.contains(split_barrier_end_pass)) {
          barrier_dst_list_ptr.back() = &barrier_before_pass;
        } else {
          barrier_dst_list_ptr.back() = &barrier_after_pass;
        }
        continue;
      }
      if (!split_barrier_end_pass) continue;
      auto split = inter_pass_distance_map.at(split_barrier_begin_pass).at(split_barrier_end_pass) > 1;
      if (split_barrier_begin_pass != split_barrier_end_pass) {
        split |= !state_change_info.pass_list_to_access_prev_buffer_state.contains(split_barrier_begin_pass) || !state_change_info.pass_list_to_access_next_buffer_state.contains(split_barrier_end_pass);
      } else {
        split |= !state_change_info.pass_list_to_access_prev_buffer_state.contains(split_barrier_begin_pass) && !state_change_info.pass_list_to_access_next_buffer_state.contains(split_barrier_end_pass);
      }
      split |= render_pass_id_map.at(split_barrier_begin_pass).command_queue_type != render_pass_id_map.at(split_barrier_end_pass).command_queue_type;
      if (!split) continue;
      barrier_info_list.back().split_type = BarrierSplitType::kBegin;
      barrier_pass_name.push_back(split_barrier_end_pass);
      if (state_change_info.pass_list_to_access_next_buffer_state.contains(split_barrier_end_pass)) {
        barrier_dst_list_ptr.push_back(&barrier_before_pass);
      } else {
        barrier_dst_list_ptr.push_back(&barrier_after_pass);
      }
      barrier_info_list.push_back(barrier_info_list.back());
      barrier_info_list.back().split_type = BarrierSplitType::kEnd;
    }
  }
  for (uint32_t i = 0; i < barrier_dst_list_ptr.size(); i++) {
    if (!barrier_dst_list_ptr[i]->contains(barrier_pass_name[i])) {
      barrier_dst_list_ptr[i]->insert({barrier_pass_name[i], std::pmr::vector<BarrierConfig>{memory_resource}});
    }
    barrier_dst_list_ptr[i]->at(barrier_pass_name[i]).push_back(std::move(barrier_info_list[i]));
  }
  return {std::move(barrier_before_pass), std::move(barrier_after_pass)};
}
static std::pmr::unordered_set<StrId> GetCommonAncestors(const std::pmr::unordered_set<StrId>& pass_list, const RenderPassIdMap& render_pass_id_map, const InterPassDistanceMap& inter_pass_distance_map, const int32_t valid_queues, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<StrId> ret{memory_resource};
  auto it = pass_list.begin();
  for (auto& [pass, distance] : inter_pass_distance_map.at(*it)) {
    if (distance <= 0 && IsContainingCommandQueueType(valid_queues, render_pass_id_map.at(pass).command_queue_type)) {
      ret.insert(pass);
    }
  }
  it++;
  while (it != pass_list.end()) {
    auto& map = inter_pass_distance_map.at(*it);
    auto ret_it = ret.begin();
    while (ret_it != ret.end()) {
      if (!map.contains(*ret_it) || map.at(*ret_it) > 0) {
        ret_it = ret.erase(ret_it);
      } else {
        ret_it++;
      }
    }
    it++;
  }
  return ret;
}
static std::pmr::unordered_set<StrId> GetCommonDescendents(const std::pmr::unordered_set<StrId>& pass_list, const RenderPassIdMap& render_pass_id_map, const InterPassDistanceMap& inter_pass_distance_map, const int32_t valid_queues, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_set<StrId> ret{memory_resource};
  auto it = pass_list.begin();
  for (auto& [pass, distance] : inter_pass_distance_map.at(*it)) {
    if (distance >= 0 && IsContainingCommandQueueType(valid_queues, render_pass_id_map.at(pass).command_queue_type)) {
      ret.insert(pass);
    }
  }
  it++;
  while (it != pass_list.end()) {
    auto& map = inter_pass_distance_map.at(*it);
    auto ret_it = ret.begin();
    while (ret_it != ret.end()) {
      if (!map.contains(*ret_it) || map.at(*ret_it) < 0) {
        ret_it = ret.erase(ret_it);
      } else {
        ret_it++;
      }
    }
    it++;
  }
  return ret;
}
std::tuple<PassBarrierInfoSet, PassBarrierInfoSet> ConfigureBarrierForNextFrame(const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferStateList& buffer_state_before_render_pass_list, const BufferStateList& buffer_state_after_render_pass_list, const InterPassDistanceMap& inter_pass_distance_map, const BufferStateChangeInfoList& state_change_info_list, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<CommandQueueType, StrId> first_pass_per_queue{memory_resource}, last_pass_per_queue{memory_resource};
  for (auto& pass_name : render_pass_order) {
    auto& command_queue_type = render_pass_id_map.at(pass_name).command_queue_type;
    if (!first_pass_per_queue.contains(command_queue_type)) {
      first_pass_per_queue.insert({command_queue_type, pass_name});
    }
    last_pass_per_queue.insert_or_assign(command_queue_type, pass_name);
  }
  PassBarrierInfoSet current_frame_barriers, next_frame_barriers;
  for (std::pmr::unordered_set<StrId> current_frame_cands{memory_resource}, next_frame_cands{memory_resource}; auto& [buffer_id, state_change_info] : state_change_info_list) {
    if (!buffer_state_before_render_pass_list.contains(buffer_id)) continue;
    if (buffer_state_after_render_pass_list.contains(buffer_id)) continue;
    auto& begin_info = state_change_info.back();
    auto& end_info = state_change_info.front();
    auto& begin_state = begin_info.next_buffer_state;
    auto& end_state = end_info.prev_buffer_state;
    if (begin_state == end_state) continue;
    current_frame_cands.clear();
    next_frame_cands.clear();
    auto valid_queues = (GetValidCommandQueues(begin_state) & GetValidCommandQueues(end_state));
    auto& last_pass = begin_info.pass_list_to_access_next_buffer_state;
    current_frame_cands = GetCommonDescendents(last_pass, render_pass_id_map, inter_pass_distance_map, valid_queues, memory_resource);
    auto& first_pass = end_info.pass_list_to_access_prev_buffer_state;
    next_frame_cands = GetCommonAncestors(first_pass, render_pass_id_map, inter_pass_distance_map, valid_queues, memory_resource);
    int32_t max_distance = 0;
    StrId begin_pass, end_pass;
    for (auto& begin_pass_cand : current_frame_cands) {
      auto& command_queue_type_begin = render_pass_id_map.at(begin_pass_cand).command_queue_type;
      auto& next_frame_first_pass = first_pass_per_queue.at(command_queue_type_begin);
      auto distance_to_next_frame_start = inter_pass_distance_map.at(begin_pass_cand).at(last_pass_per_queue.at(command_queue_type_begin)) + 1;
      for (auto& end_pass_cand : next_frame_cands) {
        if (!inter_pass_distance_map.at(end_pass_cand).contains(next_frame_first_pass)) continue;
        auto& command_queue_type_end = render_pass_id_map.at(end_pass_cand).command_queue_type;
        auto distance = distance_to_next_frame_start + inter_pass_distance_map.at(first_pass_per_queue.at(command_queue_type_end)).at(end_pass_cand);
        if (max_distance < distance) {
          max_distance = distance;
          begin_pass = begin_pass_cand;
          end_pass = end_pass_cand;
        }
      }
    }
    for (auto& begin_pass_cand : current_frame_cands) {
      for (auto& end_pass_cand : current_frame_cands) {
        if (auto distance = inter_pass_distance_map.at(begin_pass_cand).at(end_pass_cand); (!begin_pass || (distance >= 0 && max_distance < distance))) {
          max_distance = distance;
          begin_pass = begin_pass_cand;
          end_pass = end_pass_cand;
        }
      }
    }
    for (auto& begin_pass_cand : next_frame_cands) {
      for (auto& end_pass_cand : next_frame_cands) {
        if (auto distance = inter_pass_distance_map.at(begin_pass_cand).at(end_pass_cand); (!!begin_pass || (distance >= 0 && max_distance < distance))) {
          max_distance = distance;
          begin_pass = begin_pass_cand;
          end_pass = end_pass_cand;
        }
      }
    }
    if (!begin_pass || !end_pass) {
      logerror("ConfigureBarrierForNextFrame. valid barrier pass not found. {} {} {}", buffer_id, begin_pass, end_pass);
      continue;
    }
    if (begin_pass == end_pass && current_frame_cands.contains(begin_pass) && next_frame_cands.contains(begin_pass)) {
      // split
      {
        // begin
        auto barrier_container = &current_frame_barriers.barrier_after_pass;
        if (!barrier_container->contains(begin_pass)) {
          barrier_container->insert({begin_pass, std::pmr::vector<BarrierConfig>{memory_resource}});
        }
        barrier_container->at(begin_pass).push_back({buffer_id, begin_state, end_state, BarrierSplitType::kEnd});
      }
      {
        // end
        auto barrier_container = &next_frame_barriers.barrier_before_pass;
        if (!barrier_container->contains(begin_pass)) {
          barrier_container->insert({begin_pass, std::pmr::vector<BarrierConfig>{memory_resource}});
        }
        barrier_container->at(begin_pass).push_back({buffer_id, begin_state, end_state, BarrierSplitType::kEnd});
      }
      continue;
    }
    PassBarrierInfo* barrier_container_to_insert_begin = nullptr;
    {
      if (current_frame_cands.contains(begin_pass)) {
        barrier_container_to_insert_begin = last_pass.contains(begin_pass) ? &current_frame_barriers.barrier_after_pass : &current_frame_barriers.barrier_before_pass;
      } else {
        barrier_container_to_insert_begin = first_pass.contains(begin_pass) ? &next_frame_barriers.barrier_before_pass : &next_frame_barriers.barrier_after_pass;
      }
      if (!barrier_container_to_insert_begin->contains(begin_pass)) {
        barrier_container_to_insert_begin->insert({begin_pass, std::pmr::vector<BarrierConfig>{memory_resource}});
      }
      barrier_container_to_insert_begin->at(begin_pass).push_back({buffer_id, begin_state, end_state, BarrierSplitType::kNone});
    }
    if (begin_pass == end_pass) continue;
    if (max_distance <= 1 && render_pass_id_map.at(begin_pass).command_queue_type == render_pass_id_map.at(end_pass).command_queue_type) continue;
    barrier_container_to_insert_begin->at(begin_pass).back().split_type = BarrierSplitType::kBegin;
    PassBarrierInfo* barrier_container_to_insert_end = nullptr;
    if (current_frame_cands.contains(end_pass)) {
      ASSERT(last_pass.contains(end_pass));
      barrier_container_to_insert_end = &current_frame_barriers.barrier_before_pass;
    } else {
      barrier_container_to_insert_end = first_pass.contains(end_pass) ? &next_frame_barriers.barrier_before_pass : &next_frame_barriers.barrier_after_pass;
    }
    if (!barrier_container_to_insert_end->contains(end_pass)) {
      barrier_container_to_insert_end->insert({end_pass, std::pmr::vector<BarrierConfig>{memory_resource}});
    }
    barrier_container_to_insert_end->at(end_pass).push_back({buffer_id, begin_state, end_state, BarrierSplitType::kEnd});
  }
  return {current_frame_barriers, next_frame_barriers};
}
}
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_size_in_bytes = 32 * 1024;
std::byte buffer[buffer_size_in_bytes]{};
std::byte buffer2[16]{};
using namespace illuminate;
using namespace illuminate::gfx;
inline auto CreateRenderPassPrez(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("prez"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kDsv)
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassGBuffer(std::pmr::memory_resource* memory_resource, const bool with_prez = true) {
  return RenderPass(
    StrId("gbuffer"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(with_prez ? BufferLoadOpType::kLoadReadOnly : BufferLoadOpType::kClear),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kRtv),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kRtv),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassAo(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("ao"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("ao"), BufferStateType::kUav),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassLighting(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("lighting"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("shadowtex"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassPostProcess(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("postprocess"),
    {
      {
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav).LoadOpType(BufferLoadOpType::kLoadReadOnly),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassShadowMap(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("shadowmap"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kDsv).Size(BufferSizeType::kAbsolute, 1024, 1024)
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassDeferredShadowHard(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("deferredshadow-hard"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("shadowtex-hard"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassDeferredShadowPcss(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("deferredshadow-pcss"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("shadowtex-pcss"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassDebug(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("debug"),
    {
      {
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kRtv),
      },
      memory_resource
    }
  );
}
inline auto CreateRenderPassTransferTexture(std::pmr::memory_resource* memory_resource) {
  return RenderPass(StrId("transfer"), BufferConfigList(memory_resource)).Mandatory(true);
}
auto CreateRenderPassTransparent(std::pmr::memory_resource* memory_resource) {
  return RenderPass(
    StrId("transparent"),
    {
      {
        BufferConfig(StrId("mainbuffer"), BufferStateType::kRtv).LoadOpType(BufferLoadOpType::kLoadWrite),
      },
      memory_resource
    }
  );
}
auto CreateRenderPassListSimple(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list{memory_resource};
  render_pass_list.push_back({CreateRenderPassGBuffer(memory_resource, false)});
  render_pass_list.push_back({CreateRenderPassLighting(memory_resource)});
  render_pass_list.push_back({CreateRenderPassPostProcess(memory_resource)});
  return render_pass_list;
}
auto CreateRenderPassListShadow(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list{memory_resource};
  render_pass_list.push_back({CreateRenderPassPrez(memory_resource)});
  render_pass_list.push_back({CreateRenderPassShadowMap(memory_resource)});
  render_pass_list.push_back({CreateRenderPassGBuffer(memory_resource)});
  render_pass_list.push_back({CreateRenderPassDeferredShadowHard(memory_resource)});
  render_pass_list.push_back({CreateRenderPassDeferredShadowPcss(memory_resource)});
  render_pass_list.push_back({CreateRenderPassLighting(memory_resource)});
  render_pass_list.push_back({CreateRenderPassPostProcess(memory_resource)});
  return render_pass_list;
}
auto CreateRenderPassListDebug(std::pmr::memory_resource* memory_resource) {
  auto render_pass_list = CreateRenderPassListShadow(memory_resource);
  render_pass_list.push_back({CreateRenderPassDebug(memory_resource)});
  return render_pass_list;
}
auto CreateRenderPassListSkyboxCreation(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list{memory_resource};
  render_pass_list.push_back(RenderPass(
    StrId("skybox-a"),
    {
      {
        BufferConfig(StrId("skybox-tmp"), BufferStateType::kRtv).Size(BufferSizeType::kAbsolute, 1024, 1024),
      },
      memory_resource
    }
  ));
  render_pass_list.push_back(RenderPass(
    StrId("skybox-b"),
    {
      {
        BufferConfig(StrId("skybox-tmp"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("skybox"), BufferStateType::kRtv).Size(BufferSizeType::kAbsolute, 1024, 1024).Dimension(BufferDimensionType::kCube).RenderTargetIndex(3),
      },
      memory_resource
    }
  ));
  return render_pass_list;
}
auto InsertRenderPassListSkyboxCreation(RenderPassList&& render_pass_list, std::pmr::memory_resource* memory_resource) {
  auto render_pass_list_skybox = CreateRenderPassListSkyboxCreation(memory_resource);
  render_pass_list.reserve(render_pass_list.size() + render_pass_list_skybox.size());
  std::move(std::begin(render_pass_list_skybox), std::end(render_pass_list_skybox), std::back_inserter(render_pass_list));
  return std::move(render_pass_list);
}
auto CreateRenderPassListWithSkyboxCreation(std::pmr::memory_resource* memory_resource) {
  return InsertRenderPassListSkyboxCreation(CreateRenderPassListSimple(memory_resource), memory_resource);
}
auto CreateRenderPassListTransferTexture(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list_transfer{memory_resource};
  render_pass_list_transfer.push_back(CreateRenderPassTransferTexture(memory_resource));
  auto render_pass_list = CreateRenderPassListSimple(memory_resource);
  render_pass_list_transfer.reserve(render_pass_list_transfer.size() + render_pass_list.size());
  std::move(std::begin(render_pass_list), std::end(render_pass_list), std::back_inserter(render_pass_list_transfer));
  return render_pass_list_transfer;
}
auto CreateRenderPassListTransparent(std::pmr::memory_resource* memory_resource) {
  auto render_pass_list = CreateRenderPassListSimple(memory_resource);
  auto it = std::find_if(render_pass_list.begin(), render_pass_list.end(), [](const RenderPass& pass) { return pass.name == StrId("postprocess"); });
  render_pass_list.insert(it, CreateRenderPassTransparent(memory_resource));
  return render_pass_list;
}
auto CreateRenderPassListCombined(std::pmr::memory_resource* memory_resource) {
  auto render_pass_list = CreateRenderPassListShadow(memory_resource);
  render_pass_list.insert(render_pass_list.begin(), CreateRenderPassTransferTexture(memory_resource));
  render_pass_list.insert(std::find_if(render_pass_list.begin(), render_pass_list.end(), [](const RenderPass& pass) { return pass.name == StrId("postprocess"); }), CreateRenderPassTransparent(memory_resource));
  return InsertRenderPassListSkyboxCreation(std::move(render_pass_list), memory_resource);
}
auto CreateRenderPassListAsyncComputeIntraFrame(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list{memory_resource};
  render_pass_list.push_back(CreateRenderPassPrez(memory_resource));
  render_pass_list.push_back(CreateRenderPassShadowMap(memory_resource).AsyncComputeGroup(StrId("shadowmap")));
  render_pass_list.push_back(CreateRenderPassAo(memory_resource).CommandQueueTypeCompute().AsyncComputeGroup(StrId("shadowmap")));
  render_pass_list.push_back(CreateRenderPassGBuffer(memory_resource));
  render_pass_list.push_back(CreateRenderPassDeferredShadowPcss(memory_resource));
  render_pass_list.push_back(RenderPass(
    StrId("lighting"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("shadowtex"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("ao"), BufferStateType::kUav).LoadOpType(BufferLoadOpType::kLoadReadOnly),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource
    }
  ).CommandQueueTypeCompute());
  render_pass_list.push_back(CreateRenderPassPostProcess(memory_resource).CommandQueueTypeCompute());
  return render_pass_list;
}
auto CreateRenderPassListAsyncComputeInterFrame(std::pmr::memory_resource* memory_resource) {
  RenderPassList render_pass_list{memory_resource};
  render_pass_list.push_back(CreateRenderPassPrez(memory_resource).AsyncComputeGroup(StrId("prez")));
  render_pass_list.push_back(CreateRenderPassShadowMap(memory_resource));
  render_pass_list.push_back(CreateRenderPassAo(memory_resource).CommandQueueTypeCompute().AsyncComputeGroup(StrId("prez")));
  render_pass_list.push_back(CreateRenderPassGBuffer(memory_resource));
  render_pass_list.push_back(RenderPass(
    StrId("deferredshadow-pcss"),
    {
      {
        BufferConfig(StrId("shadowmap"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("shadowtex-pcss"), BufferStateType::kUav),
      },
      memory_resource
    }
  ).CommandQueueTypeCompute().EnableAsyncCompute());
  render_pass_list.push_back(RenderPass(
    StrId("lighting"),
    {
      {
        BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer0"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer1"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("gbuffer2"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("shadowtex"), BufferStateType::kSrvPsOnly),
        BufferConfig(StrId("ao"), BufferStateType::kUav).LoadOpType(BufferLoadOpType::kLoadReadOnly),
        BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
      },
      memory_resource
    }
  ).CommandQueueTypeCompute().EnableAsyncCompute());
  render_pass_list.push_back(CreateRenderPassPostProcess(memory_resource).CommandQueueTypeCompute().EnableAsyncCompute());
  return render_pass_list;
}
auto CreateAsyncComputeGroupInfo(StrId&& group_name, const AsyncComputeBatchPairType pair_type, std::pmr::memory_resource* memory_resource) {
  AsyncComputePairInfo async_compute_pair_info{memory_resource};
  async_compute_pair_info.insert({std::move(group_name), pair_type});
  return async_compute_pair_info;
}
BufferIdList CreateBufferIdList(const BatchInfoList& batch_info_list, const RenderPassIdMap& render_pass_id_map, std::pmr::memory_resource* memory_resource) {
  BufferIdList buffer_id_list{memory_resource};
  BufferId new_id = 0;
  std::pmr::unordered_map<StrId, BufferId> known_buffer{memory_resource};
  for (auto& batch : batch_info_list) {
    for (auto& pass_name : batch) {
      auto& pass_buffer_ids = buffer_id_list.insert({pass_name, PassBufferIdList{memory_resource}}).first->second;
      auto& pass = render_pass_id_map.at(pass_name);
      for (auto& buffer_config : pass.buffer_list) {
        if (buffer_config.load_op_type == BufferLoadOpType::kDontCare || buffer_config.load_op_type == BufferLoadOpType::kClear || !known_buffer.contains(buffer_config.name)) {
          pass_buffer_ids.push_back(new_id);
          known_buffer.insert({buffer_config.name, new_id});
          new_id++;
        } else {
          pass_buffer_ids.push_back(known_buffer.at(buffer_config.name));
        }
      }
    }
  }
  return buffer_id_list;
}
}
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfloat-equal"
#endif
#include "doctest/doctest.h"
TEST_CASE("BufferConfig") {
  static_assert(std::size(kCommandQueueTypeSet) == kCommandQueueTypeNum);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).name == StrId("rtv"));
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).state_type == BufferStateType::kRtv);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).width == 1.0f);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("rtv"), BufferStateType::kRtv).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).index_to_render == 0);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("rtv"), BufferStateType::kRtv).depth == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).name == StrId("srv"));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).state_type == BufferStateType::kSrvPsOnly);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).width == 1.0f);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).index_to_render == 0);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvPsOnly).depth == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).name == StrId("srv"));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).state_type == BufferStateType::kSrvNonPs);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).width == 1.0f);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).index_to_render == 0);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvNonPs).depth == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).name == StrId("srv"));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).state_type == BufferStateType::kSrvAll);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).width == 1.0f);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).index_to_render == 0);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("srv"), BufferStateType::kSrvAll).depth == 1);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).name == StrId("cbv"));
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).state_type == BufferStateType::kCbv);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).format == BufferFormat::kUnknown);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).width == 1.0f);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("cbv"), BufferStateType::kCbv).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).dimension_type == BufferDimensionType::kBuffer);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).index_to_render == 0);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("cbv"), BufferStateType::kCbv).depth == 1);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).name == StrId("uav"));
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).state_type == BufferStateType::kUav);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).width == 1.0f);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("uav"), BufferStateType::kUav).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).index_to_render == 0);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("uav"), BufferStateType::kUav).depth == 1);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).name == StrId("dsv"));
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).state_type == BufferStateType::kDsv);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).load_op_type == BufferLoadOpType::kClear);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).format == BufferFormat::kD32Float);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).width == 1.0f);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).height == 1.0f);
  CHECK(GetClearValueDepthBuffer(BufferConfig(StrId("dsv"), BufferStateType::kDsv).clear_value).depth == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).depth);
  CHECK(GetClearValueDepthBuffer(BufferConfig(StrId("dsv"), BufferStateType::kDsv).clear_value).stencil == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).stencil);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).index_to_render == 0);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("dsv"), BufferStateType::kDsv).depth == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).name == StrId("copysrc"));
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).state_type == BufferStateType::kCopySrc);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).load_op_type == BufferLoadOpType::kLoadReadOnly);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).width == 1.0f);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).index_to_render == 0);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).depth == 1);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).name == StrId("copydst"));
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).state_type == BufferStateType::kCopyDst);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).load_op_type == BufferLoadOpType::kDontCare);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).size_type == BufferSizeType::kMainbufferRelative);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).width == 1.0f);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).height == 1.0f);
  CHECK(GetClearValueColorBuffer(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).dimension_type == BufferDimensionType::k2d);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).index_to_render == 0);
  CHECK(BufferConfig(StrId("copydst"), BufferStateType::kCopyDst).buffer_num_to_render == 1);
  CHECK(BufferConfig(StrId("copysrc"), BufferStateType::kCopySrc).depth == 1);
  BufferConfig func_check(StrId("func-check"), BufferStateType::kRtv);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kDontCare);
  CHECK(func_check.format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(func_check.size_type == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.width == 1.0f);
  CHECK(func_check.height == 1.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
  CHECK(func_check.LoadOpType(BufferLoadOpType::kLoadWrite).load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(func_check.size_type == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.width == 1.0f);
  CHECK(func_check.height == 1.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
  CHECK(func_check.Format(BufferFormat::kD32Float).format == BufferFormat::kD32Float);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kMainbufferRelative);
  CHECK(func_check.width == 1.0f);
  CHECK(func_check.height == 1.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
  CHECK(func_check.Size(BufferSizeType::kAbsolute, 123, 456).size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
  CHECK(GetClearValueColorBuffer(func_check.ClearValue(ClearValue(std::array<float, 4>{123.0f, 456.0f, 789.0f, 100.0f})).clear_value)[0] == 123.0f);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 1);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.Depth(123).depth == 123);
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k2d);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 123);
  CHECK(func_check.Dimension(BufferDimensionType::k3d).dimension_type == BufferDimensionType::k3d);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k3d);
  CHECK(func_check.index_to_render == 0);
  CHECK(func_check.buffer_num_to_render == 1);
  CHECK(func_check.depth == 123);
  CHECK(func_check.RenderTargetIndex(12, 34).index_to_render == 12);
  CHECK(func_check.name == StrId("func-check"));
  CHECK(func_check.state_type == BufferStateType::kRtv);
  CHECK(func_check.load_op_type == BufferLoadOpType::kLoadWrite);
  CHECK(func_check.format == BufferFormat::kD32Float);
  CHECK(func_check.size_type == BufferSizeType::kAbsolute);
  CHECK(func_check.width == 123.0f);
  CHECK(func_check.height == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[0] == 123.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[1] == 456.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[2] == 789.0f);
  CHECK(GetClearValueColorBuffer(func_check.clear_value)[3] == 100.0f);
  CHECK(func_check.dimension_type == BufferDimensionType::k3d);
  CHECK(func_check.index_to_render == 12);
  CHECK(func_check.buffer_num_to_render == 34);
  CHECK(func_check.depth == 123);
}
TEST_CASE("CreateRenderPassListSimple") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListSimple(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  CHECK(render_pass_id_map.size() == 3);
  CHECK(render_pass_id_map[StrId("gbuffer")].name == StrId("gbuffer"));
  CHECK(render_pass_id_map[StrId("lighting")].name == StrId("lighting"));
  CHECK(render_pass_id_map[StrId("postprocess")].name == StrId("postprocess"));
  CHECK(render_pass_order.size() == 3);
  CHECK(render_pass_order[0] == StrId("gbuffer"));
  CHECK(render_pass_order[1] == StrId("lighting"));
  CHECK(render_pass_order[2] == StrId("postprocess"));
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  CHECK(buffer_id_list.size() == 3);
  CHECK(buffer_id_list[StrId("gbuffer")].size() == 4);
  CHECK(buffer_id_list[StrId("gbuffer")][0] == 0);
  CHECK(buffer_id_list[StrId("gbuffer")][1] == 1);
  CHECK(buffer_id_list[StrId("gbuffer")][2] == 2);
  CHECK(buffer_id_list[StrId("gbuffer")][3] == 3);
  CHECK(buffer_id_list[StrId("lighting")].size() == 6);
  CHECK(buffer_id_list[StrId("lighting")][0] == 0);
  CHECK(buffer_id_list[StrId("lighting")][1] == 1);
  CHECK(buffer_id_list[StrId("lighting")][2] == 2);
  CHECK(buffer_id_list[StrId("lighting")][3] == 3);
  CHECK(buffer_id_list[StrId("lighting")][4] == 4);
  CHECK(buffer_id_list[StrId("lighting")][5] == 5);
  CHECK(buffer_id_list[StrId("postprocess")].size() == 2);
  CHECK(buffer_id_list[StrId("postprocess")][0] == 5);
  CHECK(buffer_id_list[StrId("postprocess")][1] == 6);
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[0][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[1][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[2][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[3][0] == StrId("gbuffer"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[5][0] == StrId("lighting"));
  CHECK(render_pass_adjacency_graph.output_buffer_producer_pass[6][0] == StrId("postprocess"));
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][0] == 0);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][1] == 1);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][2] == 2);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][3] == 3);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("lighting")][4] == 4);
  CHECK(render_pass_adjacency_graph.consumer_pass_input_buffer[StrId("postprocess")][0] == 5);
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer")}, memory_resource.get());
  CHECK(mandatory_buffer_id_list.size() == 1);
  CHECK(mandatory_buffer_id_list[StrId("mainbuffer")] == 6);
  auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
  CHECK(used_render_pass_list.size() == 1);
  CHECK(used_render_pass_list.contains(StrId("postprocess")));
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  CHECK(consumer_producer_render_pass_map.size() == 2);
  CHECK(consumer_producer_render_pass_map.contains(StrId("lighting")));
  CHECK(consumer_producer_render_pass_map.contains(StrId("postprocess")));
  CHECK(consumer_producer_render_pass_map.at(StrId("lighting")).size() == 1);
  CHECK(consumer_producer_render_pass_map.at(StrId("lighting")).contains(StrId("gbuffer")));
  CHECK(consumer_producer_render_pass_map.at(StrId("postprocess")).size() == 1);
  CHECK(consumer_producer_render_pass_map.at(StrId("postprocess")).contains(StrId("lighting")));
  used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
  CHECK(used_render_pass_list.size() == 3);
  CHECK(used_render_pass_list.contains(StrId("gbuffer")));
  CHECK(used_render_pass_list.contains(StrId("lighting")));
  CHECK(used_render_pass_list.contains(StrId("postprocess")));
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 3);
  CHECK(culled_render_pass_order[0] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[1] == StrId("lighting"));
  CHECK(culled_render_pass_order[2] == StrId("postprocess"));
}
TEST_CASE("CreateRenderPassListShadow") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListShadow(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  CHECK(buffer_id_list.size() == 7);
  CHECK(buffer_id_list[StrId("prez")].size() == 1);
  CHECK(buffer_id_list[StrId("prez")][0] == 0);
  CHECK(buffer_id_list[StrId("shadowmap")].size() == 1);
  CHECK(buffer_id_list[StrId("shadowmap")][0] == 1);
  CHECK(buffer_id_list[StrId("gbuffer")].size() == 4);
  CHECK(buffer_id_list[StrId("gbuffer")][0] == 0);
  CHECK(buffer_id_list[StrId("gbuffer")][1] == 2);
  CHECK(buffer_id_list[StrId("gbuffer")][2] == 3);
  CHECK(buffer_id_list[StrId("gbuffer")][3] == 4);
  CHECK(buffer_id_list[StrId("deferredshadow-hard")].size() == 2);
  CHECK(buffer_id_list[StrId("deferredshadow-hard")][0] == 1);
  CHECK(buffer_id_list[StrId("deferredshadow-hard")][1] == 5);
  CHECK(buffer_id_list[StrId("deferredshadow-pcss")].size() == 2);
  CHECK(buffer_id_list[StrId("deferredshadow-pcss")][0] == 1);
  CHECK(buffer_id_list[StrId("deferredshadow-pcss")][1] == 6);
  CHECK(buffer_id_list[StrId("lighting")].size() == 6);
  CHECK(buffer_id_list[StrId("lighting")][0] == 0);
  CHECK(buffer_id_list[StrId("lighting")][1] == 2);
  CHECK(buffer_id_list[StrId("lighting")][2] == 3);
  CHECK(buffer_id_list[StrId("lighting")][3] == 4);
  CHECK(buffer_id_list[StrId("lighting")][4] == 7);
  CHECK(buffer_id_list[StrId("lighting")][5] == 8);
  CHECK(buffer_id_list[StrId("postprocess")].size() == 2);
  CHECK(buffer_id_list[StrId("postprocess")][0] == 8);
  CHECK(buffer_id_list[StrId("postprocess")][1] == 9);
  BufferNameAliasList buffer_name_alias_list{memory_resource.get()};
  SUBCASE("shadow-hard") {
    buffer_name_alias_list.insert({StrId("shadowtex-hard"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied.size() == 7);
    CHECK(buffer_id_list_alias_applied[StrId("prez")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("prez")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")].size() == 4);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == 5);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][1] == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")].size() == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][4] == 5);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][5] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][0] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][1] == 9);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, memory_resource.get());
    auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
    CHECK(used_render_pass_list.size() == 6);
    CHECK(used_render_pass_list.contains(StrId("prez")));
    CHECK(used_render_pass_list.contains(StrId("shadowmap")));
    CHECK(used_render_pass_list.contains(StrId("gbuffer")));
    CHECK(used_render_pass_list.contains(StrId("deferredshadow-hard")));
    CHECK(used_render_pass_list.contains(StrId("lighting")));
    CHECK(used_render_pass_list.contains(StrId("postprocess")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 6);
    CHECK(culled_render_pass_order[0] == StrId("prez"));
    CHECK(culled_render_pass_order[1] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[2] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[3] == StrId("deferredshadow-hard"));
    CHECK(culled_render_pass_order[4] == StrId("lighting"));
    CHECK(culled_render_pass_order[5] == StrId("postprocess"));
  }
  SUBCASE("shadow-pcss") {
    buffer_name_alias_list.insert({StrId("shadowtex-pcss"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied.size() == 7);
    CHECK(buffer_id_list_alias_applied[StrId("prez")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("prez")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")].size() == 1);
    CHECK(buffer_id_list_alias_applied[StrId("shadowmap")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")].size() == 4);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("gbuffer")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == 5);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][0] == 1);
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][1] == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")].size() == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][0] == 0);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][1] == 2);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][2] == 3);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][3] == 4);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][4] == 6);
    CHECK(buffer_id_list_alias_applied[StrId("lighting")][5] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")].size() == 2);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][0] == 8);
    CHECK(buffer_id_list_alias_applied[StrId("postprocess")][1] == 9);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, memory_resource.get());
    auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
    CHECK(used_render_pass_list.size() == 6);
    CHECK(used_render_pass_list.contains(StrId("prez")));
    CHECK(used_render_pass_list.contains(StrId("shadowmap")));
    CHECK(used_render_pass_list.contains(StrId("gbuffer")));
    CHECK(used_render_pass_list.contains(StrId("deferredshadow-pcss")));
    CHECK(used_render_pass_list.contains(StrId("lighting")));
    CHECK(used_render_pass_list.contains(StrId("postprocess")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 6);
    CHECK(culled_render_pass_order[0] == StrId("prez"));
    CHECK(culled_render_pass_order[1] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[2] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[3] == StrId("deferredshadow-pcss"));
    CHECK(culled_render_pass_order[4] == StrId("lighting"));
    CHECK(culled_render_pass_order[5] == StrId("postprocess"));
  }
}
TEST_CASE("CreateRenderPassListDebug") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListDebug(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer")}, memory_resource.get());
  auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
  CHECK(used_render_pass_list.size() == 3);
  CHECK(used_render_pass_list.contains(StrId("prez")));
  CHECK(used_render_pass_list.contains(StrId("gbuffer")));
  CHECK(used_render_pass_list.contains(StrId("debug")));
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 3);
  CHECK(culled_render_pass_order[0] == StrId("prez"));
  CHECK(culled_render_pass_order[1] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[2] == StrId("debug"));
}
TEST_CASE("CreateRenderPassListWithSkyboxCreation") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListWithSkyboxCreation(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
  auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
  CHECK(used_render_pass_list.size() == 5);
  CHECK(used_render_pass_list.contains(StrId("gbuffer")));
  CHECK(used_render_pass_list.contains(StrId("lighting")));
  CHECK(used_render_pass_list.contains(StrId("postprocess")));
  CHECK(used_render_pass_list.contains(StrId("skybox-a")));
  CHECK(used_render_pass_list.contains(StrId("skybox-b")));
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 5);
  CHECK(culled_render_pass_order[0] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[1] == StrId("lighting"));
  CHECK(culled_render_pass_order[2] == StrId("postprocess"));
  CHECK(culled_render_pass_order[3] == StrId("skybox-a"));
  CHECK(culled_render_pass_order[4] == StrId("skybox-b"));
}
TEST_CASE("CreateRenderPassListTransferTexture") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListTransferTexture(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
  auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 4);
  CHECK(culled_render_pass_order[0] == StrId("transfer"));
  CHECK(culled_render_pass_order[1] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[2] == StrId("lighting"));
  CHECK(culled_render_pass_order[3] == StrId("postprocess"));
}
TEST_CASE("CreateRenderPassListTransparent") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListTransparent(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  CHECK(buffer_id_list[StrId("transparent")].size() == 1);
  CHECK(buffer_id_list[StrId("transparent")][0] == buffer_id_list[StrId("lighting")][5]);
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("mainbuffer")}, memory_resource.get());
  auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  CHECK(culled_render_pass_order.size() == 4);
  CHECK(culled_render_pass_order[0] == StrId("gbuffer"));
  CHECK(culled_render_pass_order[1] == StrId("lighting"));
  CHECK(culled_render_pass_order[2] == StrId("transparent"));
  CHECK(culled_render_pass_order[3] == StrId("postprocess"));
}
TEST_CASE("CreateRenderPassListCombined") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListCombined(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  CHECK(render_pass_order.size() == 11);
  CHECK(render_pass_order[0] == StrId("transfer"));
  CHECK(render_pass_order[1] == StrId("prez"));
  CHECK(render_pass_order[2] == StrId("shadowmap"));
  CHECK(render_pass_order[3] == StrId("gbuffer"));
  CHECK(render_pass_order[4] == StrId("deferredshadow-hard"));
  CHECK(render_pass_order[5] == StrId("deferredshadow-pcss"));
  CHECK(render_pass_order[6] == StrId("lighting"));
  CHECK(render_pass_order[7] == StrId("transparent"));
  CHECK(render_pass_order[8] == StrId("postprocess"));
  CHECK(render_pass_order[9] == StrId("skybox-a"));
  CHECK(render_pass_order[10] == StrId("skybox-b"));
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  CHECK(buffer_id_list[StrId("transparent")].size() == 1);
  CHECK(buffer_id_list[StrId("transparent")][0] == buffer_id_list[StrId("lighting")][5]);
  BufferNameAliasList buffer_name_alias_list{memory_resource.get()};
  SUBCASE("shadow-hard") {
    buffer_name_alias_list.insert({StrId("shadowtex-hard"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == buffer_id_list_alias_applied[StrId("lighting")][4]);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
    auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
    CHECK(!used_render_pass_list.contains(StrId("deferredshadow-pcss")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 10);
    CHECK(culled_render_pass_order[0] == StrId("transfer"));
    CHECK(culled_render_pass_order[1] == StrId("prez"));
    CHECK(culled_render_pass_order[2] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[3] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[4] == StrId("deferredshadow-hard"));
    CHECK(culled_render_pass_order[5] == StrId("lighting"));
    CHECK(culled_render_pass_order[6] == StrId("transparent"));
    CHECK(culled_render_pass_order[7] == StrId("postprocess"));
    CHECK(culled_render_pass_order[8] == StrId("skybox-a"));
    CHECK(culled_render_pass_order[9] == StrId("skybox-b"));
  }
  SUBCASE("shadow-pcss") {
    buffer_name_alias_list.insert({StrId("shadowtex-pcss"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-pcss")][1] == buffer_id_list_alias_applied[StrId("lighting")][4]);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer"), StrId("skybox")}, memory_resource.get());
    auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
    CHECK(!used_render_pass_list.contains(StrId("deferredshadow-hard")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 10);
    CHECK(culled_render_pass_order[0] == StrId("transfer"));
    CHECK(culled_render_pass_order[1] == StrId("prez"));
    CHECK(culled_render_pass_order[2] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[3] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[4] == StrId("deferredshadow-pcss"));
    CHECK(culled_render_pass_order[5] == StrId("lighting"));
    CHECK(culled_render_pass_order[6] == StrId("transparent"));
    CHECK(culled_render_pass_order[7] == StrId("postprocess"));
    CHECK(culled_render_pass_order[8] == StrId("skybox-a"));
    CHECK(culled_render_pass_order[9] == StrId("skybox-b"));
  }
  SUBCASE("no skybox") {
    buffer_name_alias_list.insert({StrId("shadowtex-hard"), StrId("shadowtex")});
    auto buffer_id_list_alias_applied = ApplyBufferNameAlias(render_pass_id_map, render_pass_order, std::move(buffer_id_list), buffer_name_alias_list, memory_resource.get());
    CHECK(buffer_id_list_alias_applied[StrId("deferredshadow-hard")][1] == buffer_id_list_alias_applied[StrId("lighting")][4]);
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, memory_resource.get());
    auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list_alias_applied, {StrId("mainbuffer")}, memory_resource.get());
    auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
    CHECK(!used_render_pass_list.contains(StrId("deferredshadow-pcss")));
    auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    CHECK(culled_render_pass_order.size() == 8);
    CHECK(culled_render_pass_order[0] == StrId("transfer"));
    CHECK(culled_render_pass_order[1] == StrId("prez"));
    CHECK(culled_render_pass_order[2] == StrId("shadowmap"));
    CHECK(culled_render_pass_order[3] == StrId("gbuffer"));
    CHECK(culled_render_pass_order[4] == StrId("deferredshadow-hard"));
    CHECK(culled_render_pass_order[5] == StrId("lighting"));
    CHECK(culled_render_pass_order[6] == StrId("transparent"));
    CHECK(culled_render_pass_order[7] == StrId("postprocess"));
  }
}
TEST_CASE("RenderPassNameDupCheck") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListSimple(memory_resource.get());
  CHECK(!IsDuplicateRenderPassNameExists(render_pass_list, memory_resource.get()));
  render_pass_list.push_back(render_pass_list[0]);
  CHECK(IsDuplicateRenderPassNameExists(render_pass_list, memory_resource.get()));
}
TEST_CASE("AllocateAlignedAddressFromFreeMemory") {
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  std::pmr::vector<Address> free_memory(memory_resource.get());
  free_memory = {{{0, 100}}, memory_resource.get()};
  CHECK(AllocateAlignedAddressFromFreeMemory(20, 4, &free_memory) == 0);
  CHECK(free_memory.size() == 1);
  CHECK(free_memory[0].head_address == 20);
  CHECK(free_memory[0].size_in_bytes == 80);
  CHECK(AllocateAlignedAddressFromFreeMemory(20, 8, &free_memory) == 24);
  CHECK(free_memory.size() == 2);
  CHECK(free_memory[0].head_address == 20);
  CHECK(free_memory[0].size_in_bytes == 4);
  CHECK(free_memory[1].head_address == 44);
  CHECK(free_memory[1].size_in_bytes == 56);
  CHECK(AllocateAlignedAddressFromFreeMemory(36, 64, &free_memory) == 64);
  CHECK(free_memory.size() == 2);
  CHECK(free_memory[0].head_address == 20);
  CHECK(free_memory[0].size_in_bytes == 4);
  CHECK(free_memory[1].head_address == 44);
  CHECK(free_memory[1].size_in_bytes == 20);
  free_memory = {{{4, 50}, {64, 50}}, memory_resource.get()};
  CHECK(AllocateAlignedAddressFromFreeMemory(50, 8, &free_memory) == 64);
  CHECK(free_memory.size() == 1);
  CHECK(free_memory[0].head_address == 4);
  CHECK(free_memory[0].size_in_bytes == 50);
}
TEST_CASE("MergeToFreeMemory") {
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  std::pmr::vector<Address> free_memory(memory_resource.get());
  MergeToFreeMemory(0, 100, &free_memory);
  CHECK(free_memory.size() == 1);
  CHECK(free_memory[0].head_address == 0);
  CHECK(free_memory[0].size_in_bytes == 100);
  free_memory = {{{0, 100}}, memory_resource.get()};
  MergeToFreeMemory(100, 150, &free_memory);
  CHECK(free_memory.size() == 1);
  CHECK(free_memory[0].head_address == 0);
  CHECK(free_memory[0].size_in_bytes == 250);
  free_memory = {{{100, 150}}, memory_resource.get()};
  MergeToFreeMemory(0, 100, &free_memory);
  CHECK(free_memory.size() == 1);
  CHECK(free_memory[0].head_address == 0);
  CHECK(free_memory[0].size_in_bytes == 250);
  free_memory = {{{0, 50},{100, 150}}, memory_resource.get()};
  MergeToFreeMemory(50, 50, &free_memory);
  CHECK(free_memory.size() == 1);
  CHECK(free_memory[0].head_address == 0);
  CHECK(free_memory[0].size_in_bytes == 250);
  free_memory = {{{100, 150},{0, 50},}, memory_resource.get()};
  MergeToFreeMemory(50, 50, &free_memory);
  CHECK(free_memory.size() == 1);
  CHECK(free_memory[0].head_address == 0);
  CHECK(free_memory[0].size_in_bytes == 250);
  free_memory = {{{0, 50},{101, 150}}, memory_resource.get()};
  MergeToFreeMemory(50, 50, &free_memory);
  CHECK(free_memory.size() == 2);
  CHECK(free_memory[0].head_address == 101);
  CHECK(free_memory[0].size_in_bytes == 150);
  CHECK(free_memory[1].head_address == 0);
  CHECK(free_memory[1].size_in_bytes == 100);
  free_memory = {{{101, 150},{0, 50},}, memory_resource.get()};
  MergeToFreeMemory(50, 50, &free_memory);
  CHECK(free_memory.size() == 2);
  CHECK(free_memory[0].head_address == 101);
  CHECK(free_memory[0].size_in_bytes == 150);
  CHECK(free_memory[1].head_address == 0);
  CHECK(free_memory[1].size_in_bytes == 100);
  free_memory = {{{0, 49},{100, 150}}, memory_resource.get()};
  MergeToFreeMemory(50, 50, &free_memory);
  CHECK(free_memory.size() == 2);
  CHECK(free_memory[0].head_address == 0);
  CHECK(free_memory[0].size_in_bytes == 49);
  CHECK(free_memory[1].head_address == 50);
  CHECK(free_memory[1].size_in_bytes == 200);
  free_memory = {{{100, 150},{0, 49},}, memory_resource.get()};
  MergeToFreeMemory(50, 50, &free_memory);
  CHECK(free_memory.size() == 2);
  CHECK(free_memory[0].head_address == 0);
  CHECK(free_memory[0].size_in_bytes == 49);
  CHECK(free_memory[1].head_address == 50);
  CHECK(free_memory[1].size_in_bytes == 200);
}
TEST_CASE("buffer creation desc and allocation") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  RenderPassList render_pass_list(memory_resource.get());
  render_pass_list.push_back(RenderPass(
      StrId("1"),
      {
        {
          BufferConfig(StrId("1"), BufferStateType::kRtv),
          BufferConfig(StrId("2"), BufferStateType::kUav).Size(BufferSizeType::kAbsolute, 5, 7),
          BufferConfig(StrId("3"), BufferStateType::kDsv),
          BufferConfig(StrId("5"), BufferStateType::kRtv).Dimension(BufferDimensionType::k3d).Depth(8),
        },
        memory_resource.get()
      }
  ));
  render_pass_list.push_back(RenderPass(
      StrId("2"),
      {
        {
          BufferConfig(StrId("1"), BufferStateType::kRtv).LoadOpType(BufferLoadOpType::kLoadWrite),
          BufferConfig(StrId("2"), BufferStateType::kSrvPsOnly),
          BufferConfig(StrId("4"), BufferStateType::kRtv).Size(BufferSizeType::kSwapchainRelative, 2, 4),
        },
        memory_resource.get()
      }
  ));
  render_pass_list.push_back(RenderPass(
      StrId("3"),
      {
        {
          BufferConfig(StrId("6"), BufferStateType::kRtv).LoadOpType(BufferLoadOpType::kLoadWrite),
        },
        memory_resource.get()
      }
  ));
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto mandatory_buffer_id_list = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, {StrId("4")}, memory_resource.get());
  auto used_render_pass_list = GetBufferProducerPassList(render_pass_adjacency_graph, CreateValueSetFromMap(mandatory_buffer_id_list, memory_resource.get()), memory_resource.get());
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
  auto culled_render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
  auto buffer_creation_descs = ConfigureBufferCreationDescs(render_pass_id_map, culled_render_pass_order, buffer_id_list, {12, 34}, {56, 78}, memory_resource.get());
  CHECK(buffer_creation_descs.size() == 5);
  CHECK(buffer_creation_descs[0].initial_state_flag == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[0].state_flags == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[0].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[0].width == 12);
  CHECK(buffer_creation_descs[0].height == 34);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[0].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[0].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[0].depth == 1);
  CHECK(buffer_creation_descs[1].initial_state_flag == kBufferStateFlagUav);
  CHECK(buffer_creation_descs[1].state_flags == (kBufferStateFlagUav | kBufferStateFlagSrvPsOnly));
  CHECK(buffer_creation_descs[1].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[1].width == 5);
  CHECK(buffer_creation_descs[1].height == 7);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[1].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[1].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[1].depth == 1);
  CHECK(buffer_creation_descs[2].initial_state_flag == kBufferStateFlagDsvWrite);
  CHECK(buffer_creation_descs[2].state_flags == kBufferStateFlagDsvWrite);
  CHECK(buffer_creation_descs[2].format == BufferFormat::kD32Float);
  CHECK(buffer_creation_descs[2].width == 12);
  CHECK(buffer_creation_descs[2].height == 34);
  CHECK(GetClearValueDepthBuffer(buffer_creation_descs[2].clear_value).depth == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).depth);
  CHECK(GetClearValueDepthBuffer(buffer_creation_descs[2].clear_value).stencil == GetClearValueDepthBuffer(GetClearValueDefaultDepthBuffer()).stencil);
  CHECK(buffer_creation_descs[2].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[2].depth == 1);
  CHECK(buffer_creation_descs[3].initial_state_flag == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[3].state_flags == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[3].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[3].width == 12);
  CHECK(buffer_creation_descs[3].height == 34);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[3].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[3].dimension_type == BufferDimensionType::k3d);
  CHECK(buffer_creation_descs[3].depth == 8);
  CHECK(buffer_creation_descs[4].initial_state_flag == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[4].state_flags == kBufferStateFlagRtv);
  CHECK(buffer_creation_descs[4].format == BufferFormat::kR8G8B8A8Unorm);
  CHECK(buffer_creation_descs[4].width == 56 * 2);
  CHECK(buffer_creation_descs[4].height == 78 * 4);
  CHECK(GetClearValueColorBuffer(buffer_creation_descs[4].clear_value) == GetClearValueColorBuffer(GetClearValueDefaultColorBuffer()));
  CHECK(buffer_creation_descs[4].dimension_type == BufferDimensionType::k2d);
  CHECK(buffer_creation_descs[4].depth == 1);
  auto [physical_buffer_size_in_byte, physical_buffer_alignment] = GetPhysicalBufferSizes(buffer_creation_descs, []([[maybe_unused]] const BufferCreationDesc& desc) { return std::make_tuple<uint32_t, uint32_t>(sizeof(uint32_t), 4); }, memory_resource.get());
  CHECK(physical_buffer_size_in_byte.size() == 5);
  CHECK(physical_buffer_size_in_byte[0] == 4);
  CHECK(physical_buffer_size_in_byte[1] == 4);
  CHECK(physical_buffer_size_in_byte[2] == 4);
  CHECK(physical_buffer_size_in_byte[3] == 4);
  CHECK(physical_buffer_size_in_byte[4] == 4);
  CHECK(physical_buffer_alignment.size() == 5);
  CHECK(physical_buffer_alignment[0] == 4);
  CHECK(physical_buffer_alignment[1] == 4);
  CHECK(physical_buffer_alignment[2] == 4);
  CHECK(physical_buffer_alignment[3] == 4);
  CHECK(physical_buffer_alignment[4] == 4);
  auto [physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass] = CalculatePhysicalBufferLiftime(culled_render_pass_order, buffer_id_list, memory_resource.get());
  CHECK(physical_buffer_lifetime_begin_pass.size() == 2);
  CHECK(physical_buffer_lifetime_begin_pass.at(StrId("1")).size() == 4);
  CHECK(physical_buffer_lifetime_begin_pass.at(StrId("1"))[0] == 0);
  CHECK(physical_buffer_lifetime_begin_pass.at(StrId("1"))[1] == 1);
  CHECK(physical_buffer_lifetime_begin_pass.at(StrId("1"))[2] == 2);
  CHECK(physical_buffer_lifetime_begin_pass.at(StrId("1"))[3] == 3);
  CHECK(physical_buffer_lifetime_end_pass.at(StrId("1")).size() == 2);
  CHECK(physical_buffer_lifetime_end_pass.at(StrId("1"))[0] == 2);
  CHECK(physical_buffer_lifetime_end_pass.at(StrId("1"))[1] == 3);
  CHECK(physical_buffer_lifetime_begin_pass.at(StrId("2")).size() == 1);
  CHECK(physical_buffer_lifetime_begin_pass.at(StrId("2"))[0] == 4);
  CHECK(physical_buffer_lifetime_end_pass.at(StrId("2")).size() == 3);
  CHECK(physical_buffer_lifetime_end_pass.at(StrId("2"))[0] == 0);
  CHECK(physical_buffer_lifetime_end_pass.at(StrId("2"))[1] == 1);
  CHECK(physical_buffer_lifetime_end_pass.at(StrId("2"))[2] == 4);
  auto physical_buffer_address_offset = GetPhysicalBufferAddressOffset(culled_render_pass_order, physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass, physical_buffer_size_in_byte, physical_buffer_alignment, memory_resource.get());
  CHECK(physical_buffer_address_offset[0] == 0);
  CHECK(physical_buffer_address_offset[1] == 4);
  CHECK(physical_buffer_address_offset[2] == 8);
  CHECK(physical_buffer_address_offset[3] == 12);
  CHECK(physical_buffer_address_offset[4] == 8);
  std::pmr::unordered_map<BufferId, uint32_t*> physical_buffers{memory_resource.get()};
  for (auto& [buffer_id, offset] : physical_buffer_address_offset) {
    physical_buffers.insert({buffer_id, static_cast<uint32_t*>(static_cast<void*>(&buffer2[offset]))});
  }
  std::pmr::unordered_map<StrId, std::function<void(const PassBufferIdList&, const std::pmr::unordered_map<BufferId, uint32_t*>&)>> pass_functions{
    {
      StrId("1"),
      [](const PassBufferIdList& buffer_ids, const std::pmr::unordered_map<BufferId, uint32_t*>& physical_buffer_ptr_list) {
        *physical_buffer_ptr_list.at(buffer_ids[0]) = 255;
        *physical_buffer_ptr_list.at(buffer_ids[1]) = 512;
        *physical_buffer_ptr_list.at(buffer_ids[2]) = 1001;
        *physical_buffer_ptr_list.at(buffer_ids[3]) = 1010;
      }
    },
    {
      StrId("2"),
      [](const PassBufferIdList& buffer_ids, const std::pmr::unordered_map<BufferId, uint32_t*>& physical_buffer_ptr_list) {
        *physical_buffer_ptr_list.at(buffer_ids[0]) = *physical_buffer_ptr_list.at(buffer_ids[0]) + 1;
        *physical_buffer_ptr_list.at(buffer_ids[2]) = *physical_buffer_ptr_list.at(buffer_ids[1]) + 1024;
      }
    },
  };
  for (auto& pass_id : culled_render_pass_order) {
    pass_functions.at(pass_id)(buffer_id_list.at(pass_id), physical_buffers);
  }
  CHECK(*physical_buffers.at(0) == 256);
  CHECK(*physical_buffers.at(1) == 512);
  CHECK(*physical_buffers.at(2) == *physical_buffers.at(4));
  CHECK(*physical_buffers.at(3) == 1010);
  CHECK(*physical_buffers.at(4) == 512 + 1024);
}
TEST_CASE("ConfigureResourceDependencyBatching") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  RenderPassIdMap render_pass_id_map{memory_resource.get()};
  render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {})});
  render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {})});
  render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {}).CommandQueueTypeCompute()});
  render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {}).CommandQueueTypeCompute()});
  render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {})});
  render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {})});
  render_pass_id_map.insert({StrId("G"), RenderPass(StrId("G"), {})});
  BatchInfoList async_compute_batching{memory_resource.get()};
  async_compute_batching.push_back({
      {StrId("A"),StrId("B"),StrId("C"),StrId("D"),StrId("E"),StrId("F"),StrId("G"),},
      memory_resource.get()});
  ConsumerProducerRenderPassMap consumer_producer_render_pass_map{memory_resource.get()};
  consumer_producer_render_pass_map.insert({StrId("B"), std::pmr::unordered_set<StrId>{}});
  consumer_producer_render_pass_map.at(StrId("B")).insert(StrId("A"));
  consumer_producer_render_pass_map.insert({StrId("C"), std::pmr::unordered_set<StrId>{}});
  consumer_producer_render_pass_map.at(StrId("C")).insert(StrId("A"));
  consumer_producer_render_pass_map.insert({StrId("D"), std::pmr::unordered_set<StrId>{}});
  consumer_producer_render_pass_map.at(StrId("D")).insert(StrId("A"));
  consumer_producer_render_pass_map.insert({StrId("E"), std::pmr::unordered_set<StrId>{}});
  consumer_producer_render_pass_map.at(StrId("E")).insert(StrId("B"));
  consumer_producer_render_pass_map.at(StrId("E")).insert(StrId("D"));
  consumer_producer_render_pass_map.insert({StrId("F"), std::pmr::unordered_set<StrId>{}});
  consumer_producer_render_pass_map.at(StrId("F")).insert(StrId("D"));
  consumer_producer_render_pass_map.at(StrId("F")).insert(StrId("C"));
  consumer_producer_render_pass_map.insert({StrId("G"), std::pmr::unordered_set<StrId>{}});
  consumer_producer_render_pass_map.at(StrId("G")).insert(StrId("C"));
  auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(async_compute_batching), memory_resource.get());
  auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
  pass_signal_info = RemoveRedundantPassSignalInfo(render_pass_id_map, render_pass_order, std::move(pass_signal_info), memory_resource.get());
  CHECK(pass_signal_info.size() == 2);
  CHECK(pass_signal_info.contains(StrId("A")));
  CHECK(pass_signal_info.at(StrId("A")).size() == 1);
  CHECK(pass_signal_info.at(StrId("A")).contains(StrId("C")));
  CHECK(pass_signal_info.contains(StrId("D")));
  CHECK(pass_signal_info.at(StrId("D")).size() == 1);
  CHECK(pass_signal_info.at(StrId("D")).contains(StrId("E")));
}
TEST_CASE("AsyncComputeIntraFrame") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListAsyncComputeIntraFrame(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  BatchInfoList async_compute_batching;
  RenderPassOrder render_pass_unprocessed;
  SUBCASE("with group name") {
    auto async_group_info = CreateAsyncComputeGroupInfo(StrId("shadowmap"), AsyncComputeBatchPairType::kCurrentFrame, memory_resource.get());
    std::tie(async_compute_batching, render_pass_unprocessed) = ConfigureAsyncComputeBatching(render_pass_id_map, std::move(render_pass_order), {}, async_group_info, memory_resource.get());
    CHECK(async_compute_batching.size() == 2);
    CHECK(async_compute_batching[0].size() == 1);
    CHECK(async_compute_batching[0][0] == StrId("prez"));
    CHECK(async_compute_batching[1].size() == 6);
    CHECK(async_compute_batching[1][0] == StrId("shadowmap"));
    CHECK(async_compute_batching[1][1] == StrId("ao"));
    CHECK(async_compute_batching[1][2] == StrId("gbuffer"));
    CHECK(async_compute_batching[1][3] == StrId("deferredshadow-pcss"));
    CHECK(async_compute_batching[1][4] == StrId("lighting"));
    CHECK(async_compute_batching[1][5] == StrId("postprocess"));
    CHECK(render_pass_unprocessed.empty());
  }
  SUBCASE("no group name") {
    std::tie(async_compute_batching, render_pass_unprocessed) = ConfigureAsyncComputeBatching(render_pass_id_map, std::move(render_pass_order), {}, {}, memory_resource.get());
    CHECK(async_compute_batching.size() == 1);
    CHECK(async_compute_batching[0].size() == 7);
    CHECK(async_compute_batching[0][0] == StrId("prez"));
    CHECK(async_compute_batching[0][1] == StrId("shadowmap"));
    CHECK(async_compute_batching[0][2] == StrId("ao"));
    CHECK(async_compute_batching[0][3] == StrId("gbuffer"));
    CHECK(async_compute_batching[0][4] == StrId("deferredshadow-pcss"));
    CHECK(async_compute_batching[0][5] == StrId("lighting"));
    CHECK(async_compute_batching[0][6] == StrId("postprocess"));
    CHECK(render_pass_unprocessed.empty());
  }
}
TEST_CASE("AsyncComputeInterFrame") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto render_pass_list = CreateRenderPassListAsyncComputeInterFrame(memory_resource.get());
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
  auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
  auto async_group_info = CreateAsyncComputeGroupInfo(StrId("prez"), AsyncComputeBatchPairType::kPairComputeWithNextFrameGraphics, memory_resource.get());
  auto [async_compute_batching, render_pass_unprocessed] = ConfigureAsyncComputeBatching(render_pass_id_map, std::move(render_pass_order), {}, async_group_info, memory_resource.get());
  CHECK(async_compute_batching.size() == 1);
  CHECK(async_compute_batching[0].size() == 3);
  CHECK(async_compute_batching[0][0] == StrId("prez"));
  CHECK(async_compute_batching[0][1] == StrId("shadowmap"));
  CHECK(async_compute_batching[0][2] == StrId("gbuffer"));
  CHECK(render_pass_unprocessed.size() == 4);
  CHECK(render_pass_unprocessed[0] == StrId("ao"));
  CHECK(render_pass_unprocessed[1] == StrId("deferredshadow-pcss"));
  CHECK(render_pass_unprocessed[2] == StrId("lighting"));
  CHECK(render_pass_unprocessed[3] == StrId("postprocess"));
  auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, ConvertBatchInfoBackToRenderPassOrder(std::move(async_compute_batching), memory_resource.get()), consumer_producer_render_pass_map, memory_resource.get());
  render_pass_list = CreateRenderPassListAsyncComputeInterFrame(memory_resource.get());
  std::tie(render_pass_id_map, render_pass_order) = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  std::tie(async_compute_batching, render_pass_unprocessed) = ConfigureAsyncComputeBatching(render_pass_id_map, std::move(render_pass_order), std::move(render_pass_unprocessed), async_group_info, memory_resource.get());
  CHECK(async_compute_batching.size() == 1);
  CHECK(async_compute_batching[0].size() == 7);
  CHECK(async_compute_batching[0][0] == StrId("ao"));
  CHECK(async_compute_batching[0][1] == StrId("deferredshadow-pcss"));
  CHECK(async_compute_batching[0][2] == StrId("lighting"));
  CHECK(async_compute_batching[0][3] == StrId("postprocess"));
  CHECK(async_compute_batching[0][4] == StrId("prez"));
  CHECK(async_compute_batching[0][5] == StrId("shadowmap"));
  CHECK(async_compute_batching[0][6] == StrId("gbuffer"));
  CHECK(render_pass_unprocessed.size() == 4);
  CHECK(render_pass_unprocessed[0] == StrId("ao"));
  CHECK(render_pass_unprocessed[1] == StrId("deferredshadow-pcss"));
  CHECK(render_pass_unprocessed[2] == StrId("lighting"));
  CHECK(render_pass_unprocessed[3] == StrId("postprocess"));
  pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, ConvertBatchInfoBackToRenderPassOrder(std::move(async_compute_batching), memory_resource.get()), consumer_producer_render_pass_map, memory_resource.get());
  std::tie(async_compute_batching, render_pass_unprocessed) = ConfigureAsyncComputeBatching(render_pass_id_map, {}, std::move(render_pass_unprocessed), async_group_info, memory_resource.get());
  CHECK(async_compute_batching.size() == 1);
  CHECK(async_compute_batching[0].size() == 4);
  CHECK(async_compute_batching[0][0] == StrId("ao"));
  CHECK(async_compute_batching[0][1] == StrId("deferredshadow-pcss"));
  CHECK(async_compute_batching[0][2] == StrId("lighting"));
  CHECK(async_compute_batching[0][3] == StrId("postprocess"));
  CHECK(render_pass_unprocessed.empty());
  pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, ConvertBatchInfoBackToRenderPassOrder(std::move(async_compute_batching), memory_resource.get()), consumer_producer_render_pass_map, memory_resource.get());
}
TEST_CASE("CountSetBitNum") {
  using namespace illuminate::core;
  using namespace illuminate::gfx;
  CHECK(CountSetBitNum(0x0) == 0);
  CHECK(CountSetBitNum(0x1) == 1);
  CHECK(CountSetBitNum(0x2) == 1);
  CHECK(CountSetBitNum(0x4) == 1);
  CHECK(CountSetBitNum(0x8) == 1);
  CHECK(CountSetBitNum(0x8 | 0x1) == 2);
  CHECK(CountSetBitNum(0x8 | 0x2 | 0x1) == 3);
  CHECK(CountSetBitNum(0x8 | 0x4 | 0x2 | 0x1) == 4);
}
TEST_CASE("batch info list -> render pass order") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  BatchInfoList batch_info_list{memory_resource.get()};
  batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
  batch_info_list.back().push_back(StrId("A"));
  batch_info_list.back().push_back(StrId("B"));
  batch_info_list.back().push_back(StrId("C"));
  batch_info_list.back().push_back(StrId("D"));
  batch_info_list.back().push_back(StrId("E"));
  batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
  batch_info_list.back().push_back(StrId("F"));
  batch_info_list.back().push_back(StrId("G"));
  batch_info_list.back().push_back(StrId("H"));
  batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
  batch_info_list.back().push_back(StrId("I"));
  batch_info_list.back().push_back(StrId("J"));
  batch_info_list.back().push_back(StrId("K"));
  batch_info_list.back().push_back(StrId("L"));
  batch_info_list.back().push_back(StrId("M"));
  auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
  CHECK(render_pass_order[0] == StrId("A"));
  CHECK(render_pass_order[1] == StrId("B"));
  CHECK(render_pass_order[2] == StrId("C"));
  CHECK(render_pass_order[3] == StrId("D"));
  CHECK(render_pass_order[4] == StrId("E"));
  CHECK(render_pass_order[5] == StrId("F"));
  CHECK(render_pass_order[6] == StrId("G"));
  CHECK(render_pass_order[7] == StrId("H"));
  CHECK(render_pass_order[8] == StrId("I"));
  CHECK(render_pass_order[9] == StrId("J"));
  CHECK(render_pass_order[10] == StrId("K"));
  CHECK(render_pass_order[11] == StrId("L"));
  CHECK(render_pass_order[12] == StrId("M"));
}
TEST_CASE("batch info -> signal list") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass()});
    render_pass_id_map.insert({StrId("B"), RenderPass()});
    auto signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    CHECK(signal_info.empty());
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass()});
    render_pass_id_map.insert({StrId("B"), RenderPass()});
    render_pass_id_map.insert({StrId("C"), RenderPass()});
    auto signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    CHECK(signal_info.empty());
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass()});
    render_pass_id_map.insert({StrId("B"), RenderPass()});
    render_pass_id_map.insert({StrId("C"), RenderPass().CommandQueueTypeCompute()});
    auto signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    CHECK(signal_info.size() == 1);
    CHECK(signal_info.at(StrId("A")).size() == 1);
    CHECK(signal_info.at(StrId("A")).contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass()});
    render_pass_id_map.insert({StrId("B"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("D"), RenderPass()});
    render_pass_id_map.insert({StrId("E"), RenderPass()});
    render_pass_id_map.insert({StrId("F"), RenderPass()});
    auto signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    CHECK(signal_info.size() == 2);
    CHECK(signal_info.at(StrId("A")).size() == 1);
    CHECK(signal_info.at(StrId("A")).contains(StrId("B")));
    CHECK(signal_info.at(StrId("C")).size() == 1);
    CHECK(signal_info.at(StrId("C")).contains(StrId("D")));
  }
  {
    // command queue type graphics, compute and transfer
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    batch_info_list.back().push_back(StrId("G"));
    batch_info_list.back().push_back(StrId("H"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass()});
    render_pass_id_map.insert({StrId("B"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass().CommandQueueTypeTransfer()});
    render_pass_id_map.insert({StrId("D"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("E"), RenderPass()});
    render_pass_id_map.insert({StrId("F"), RenderPass().CommandQueueTypeTransfer()});
    render_pass_id_map.insert({StrId("G"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("H"), RenderPass().CommandQueueTypeTransfer()});
    auto signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    CHECK(signal_info.size() == 3);
    CHECK(signal_info.at(StrId("A")).size() == 2);
    CHECK(signal_info.at(StrId("A")).contains(StrId("F")));
    CHECK(signal_info.at(StrId("A")).contains(StrId("G")));
    CHECK(signal_info.at(StrId("C")).size() == 2);
    CHECK(signal_info.at(StrId("C")).contains(StrId("E")));
    CHECK(signal_info.at(StrId("C")).contains(StrId("G")));
    CHECK(signal_info.at(StrId("D")).size() == 2);
    CHECK(signal_info.at(StrId("D")).contains(StrId("E")));
    CHECK(signal_info.at(StrId("D")).contains(StrId("F")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("F"));
    batch_info_list.back().push_back(StrId("G"));
    batch_info_list.back().push_back(StrId("H"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("I"));
    batch_info_list.back().push_back(StrId("J"));
    batch_info_list.back().push_back(StrId("K"));
    batch_info_list.back().push_back(StrId("L"));
    batch_info_list.back().push_back(StrId("M"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("B"), RenderPass().CommandQueueTypeTransfer()});
    render_pass_id_map.insert({StrId("C"), RenderPass()});
    render_pass_id_map.insert({StrId("D"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("E"), RenderPass()});
    render_pass_id_map.insert({StrId("F"), RenderPass()});
    render_pass_id_map.insert({StrId("G"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("H"), RenderPass()});
    render_pass_id_map.insert({StrId("I"), RenderPass().CommandQueueTypeTransfer()});
    render_pass_id_map.insert({StrId("J"), RenderPass().CommandQueueTypeTransfer()});
    render_pass_id_map.insert({StrId("K"), RenderPass().CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("L"), RenderPass()});
    render_pass_id_map.insert({StrId("M"), RenderPass()});
    auto signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    CHECK(signal_info.size() == 5);
    CHECK(signal_info.at(StrId("B")).size() == 2);
    CHECK(signal_info.at(StrId("B")).contains(StrId("F")));
    CHECK(signal_info.at(StrId("B")).contains(StrId("G")));
    CHECK(signal_info.at(StrId("D")).size() == 1);
    CHECK(signal_info.at(StrId("D")).contains(StrId("F")));
    CHECK(signal_info.at(StrId("E")).size() == 1);
    CHECK(signal_info.at(StrId("E")).contains(StrId("G")));
    CHECK(signal_info.at(StrId("G")).size() == 2);
    CHECK(signal_info.at(StrId("G")).contains(StrId("I")));
    CHECK(signal_info.at(StrId("G")).contains(StrId("L")));
    CHECK(signal_info.at(StrId("H")).size() == 2);
    CHECK(signal_info.at(StrId("H")).contains(StrId("I")));
    CHECK(signal_info.at(StrId("H")).contains(StrId("K")));
  }
}
TEST_CASE("barrier") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagDsvRead);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    auto [current_frame_barriers, next_frame_barriers] = ConfigureBarrierForNextFrame(render_pass_id_map, render_pass_order, buffer_state_before_render_pass_list, {}, inter_pass_distance_map, buffer_state_change_list, memory_resource.get());
    CHECK(current_frame_barriers.barrier_before_pass.empty());
    CHECK(current_frame_barriers.barrier_after_pass.size() == 1);
    CHECK(current_frame_barriers.barrier_after_pass.contains(StrId("B")));
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("B"))[0].state_flag_before_pass == kBufferStateFlagDsvRead);
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("B"))[0].state_flag_after_pass == kBufferStateFlagDsvWrite);
    CHECK(next_frame_barriers.barrier_before_pass.empty());
    CHECK(next_frame_barriers.barrier_after_pass.empty());
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("-"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("-"), RenderPass(StrId("-"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagDsvRead);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("-")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("-")));
    CHECK(barrier_info.barrier_before_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagDsvRead);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvAll)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].buffer_id == 0);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    // same resource used in multiple batches
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({2, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({3, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("F")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("F")));
  }
  {
    // same resource used on same batch, different queue
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("rtv"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagRtv});
    buffer_state_before_render_pass_list.insert({2, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({3, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(barrier_info.barrier_after_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("F")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("F")));
  }
  {
    // same resource used on same batch, different queue with succeeding pass on different queue
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({2, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({3, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("F")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("F")));
  }
  {
    // same resource used on same batch, different queue with succeeding pass on same queue
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({2, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({3, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("F")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("F")));
  }
  {
    // same resource used on same batch, different queue, batch in between
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({2, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({3, kBufferStateFlagUav});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    if (barrier_info.barrier_after_pass.contains(StrId("C"))) {
      CHECK(barrier_info.barrier_after_pass[StrId("C")].size() == 1);
      CHECK(barrier_info.barrier_after_pass[StrId("C")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
      CHECK(barrier_info.barrier_after_pass[StrId("C")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
      CHECK(barrier_info.barrier_after_pass[StrId("C")][0].split_type == BarrierSplitType::kEnd);
      CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
    } else {
      CHECK(barrier_info.barrier_after_pass[StrId("D")].size() == 1);
      CHECK(barrier_info.barrier_after_pass[StrId("D")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
      CHECK(barrier_info.barrier_after_pass[StrId("D")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
      CHECK(barrier_info.barrier_after_pass[StrId("D")][0].split_type == BarrierSplitType::kEnd);
    }
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("F")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("F")));
  }
  {
    // same resource used on same batch, different queue, batch in between
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("shadowmap"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({2, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({3, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    if (barrier_info.barrier_after_pass.contains(StrId("B"))) {
      CHECK(barrier_info.barrier_after_pass[StrId("B")].size() == 1);
      CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
      CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
      CHECK(barrier_info.barrier_after_pass[StrId("B")][0].split_type == BarrierSplitType::kEnd);
      CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    } else {
      CHECK(barrier_info.barrier_after_pass[StrId("D")].size() == 1);
      CHECK(barrier_info.barrier_after_pass[StrId("D")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
      CHECK(barrier_info.barrier_after_pass[StrId("D")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
      CHECK(barrier_info.barrier_after_pass[StrId("D")][0].split_type == BarrierSplitType::kEnd);
    }
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("F")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("F")));
  }
  {
    // same resource used on same batch, different queue, batch in between (with no pass in producer queue)
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("D"));
    batch_info_list.back().push_back(StrId("E"));
    batch_info_list.back().push_back(StrId("F"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("uav"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("E"), RenderPass(StrId("E"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("F"), RenderPass(StrId("F"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({2, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({3, kBufferStateFlagUav});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("E")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("F")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("F")));
  }
  {
    // swapchain
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("main"), BufferStateType::kRtv)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagPresent});
    BufferStateList buffer_state_after_render_pass_list{memory_resource.get()};
    buffer_state_after_render_pass_list.insert({0, kBufferStateFlagPresent});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, buffer_state_after_render_pass_list, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(barrier_info.barrier_before_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
  }
  {
    // swapchain with preceding pass
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("-"));
    batch_info_list.back().push_back(StrId("A"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("-"), RenderPass(StrId("-"), {{BufferConfig(StrId("dmy"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("main"), BufferStateType::kRtv)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagRtv});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagPresent});
    BufferStateList buffer_state_after_render_pass_list{memory_resource.get()};
    buffer_state_after_render_pass_list.insert({1, kBufferStateFlagPresent});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, buffer_state_after_render_pass_list, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(barrier_info.barrier_before_pass[StrId("-")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("-")][0].state_flag_before_pass == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_before_pass[StrId("-")][0].state_flag_after_pass  == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_before_pass[StrId("-")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("-")));
    CHECK(barrier_info.barrier_before_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].split_type == BarrierSplitType::kEnd);
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
  }
  {
    // swapchain with preceding compute queue pass
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("-"));
    batch_info_list.back().push_back(StrId("A"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("-"), RenderPass(StrId("-"), {{BufferConfig(StrId("dmy"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("main"), BufferStateType::kRtv)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagPresent});
    BufferStateList buffer_state_after_render_pass_list{memory_resource.get()};
    buffer_state_after_render_pass_list.insert({1, kBufferStateFlagPresent});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, buffer_state_after_render_pass_list, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("-")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("-")));
    CHECK(barrier_info.barrier_before_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
  }
  {
    // swapchain with following pass
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("-"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("main"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("-"), RenderPass(StrId("-"), {{BufferConfig(StrId("dmy"), BufferStateType::kRtv)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagPresent});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagRtv});
    BufferStateList buffer_state_after_render_pass_list{memory_resource.get()};
    buffer_state_after_render_pass_list.insert({0, kBufferStateFlagPresent});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, buffer_state_after_render_pass_list, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(barrier_info.barrier_before_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("-")));
    CHECK(barrier_info.barrier_after_pass[StrId("-")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("-")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("-")][0].state_flag_after_pass  == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_after_pass[StrId("-")][0].split_type == BarrierSplitType::kEnd);
  }
  {
    // swapchain with following compute queue pass
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("-"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("main"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("-"), RenderPass(StrId("-"), {{BufferConfig(StrId("dmy"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagPresent});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagUav});
    BufferStateList buffer_state_after_render_pass_list{memory_resource.get()};
    buffer_state_after_render_pass_list.insert({0, kBufferStateFlagPresent});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, buffer_state_after_render_pass_list, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(barrier_info.barrier_before_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_before_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagPresent);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("-")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("-")));
  }
  {
    // rtv->srv->rtv(alpha blend)
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("main"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("main"), BufferStateType::kSrvPsOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("main"), BufferStateType::kRtv).LoadOpType(BufferLoadOpType::kLoadWrite)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagRtv});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvPsOnly);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(barrier_info.barrier_after_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagSrvPsOnly);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
#if 0
    // dsv w->r(dsv only)->w => no barrier 
    // add this case if needed for performance
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadWrite)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(barrier_info.barrier_before_pass.empty());
    CHECK(barrier_info.barrier_after_pass.empty());
#endif
  }
  {
    // dsv w->r(dsv|srv)->w  => need barrier (if copy is preferrable, add copy pass manually)
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadWrite)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(barrier_info.barrier_after_pass[StrId("C")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("C")][0].state_flag_before_pass == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_after_pass[StrId("C")][0].state_flag_after_pass  == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("C")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("D")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
  }
  {
    // tests using producer_pass_signal_list, consumer_pass_waiting_signal_list
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kUav)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    PassSignalInfo pass_signal_info{memory_resource.get()};
    pass_signal_info.insert({StrId("A"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
    pass_signal_info.at(StrId("A")).insert(StrId("B"));
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(barrier_info.barrier_before_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    // multiple read -> write test
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("D"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvPsOnly)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadWrite)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, static_cast<decltype(kBufferStateFlagDsvRead)>(kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly)});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(barrier_info.barrier_before_pass[StrId("D")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].state_flag_before_pass == (kBufferStateFlagDsvRead | kBufferStateFlagSrvPsOnly));
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].state_flag_after_pass  == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
  }
  {
    // multiple read -> unassociated path -> write test
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("-"));
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("D"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadReadOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("depth"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("-"), RenderPass(StrId("-"), {{BufferConfig(StrId("other"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("depth"), BufferStateType::kDsv).LoadOpType(BufferLoadOpType::kLoadWrite)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, static_cast<decltype(kBufferStateFlagDsvRead)>(kBufferStateFlagDsvRead | kBufferStateFlagSrvNonPs)});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagUav});
    auto pass_signal_info = ConvertBatchToSignalInfo(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("-")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("-")));
    CHECK(barrier_info.barrier_before_pass[StrId("D")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].state_flag_before_pass == (kBufferStateFlagDsvRead | kBufferStateFlagSrvNonPs));
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].state_flag_after_pass  == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvPsOnly)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("B")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_before_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagSrvPsOnly);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("B")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(barrier_info.barrier_before_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("-"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvAll)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("C")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagRtv});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(barrier_info.barrier_before_pass[StrId("C")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_after_pass  == kBufferStateFlagSrv);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("-"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("C")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagRtv});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(barrier_info.barrier_before_pass[StrId("C")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("-"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvAll)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    PassSignalInfo pass_signal_info{memory_resource.get()};
    pass_signal_info.insert({StrId("A"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
    pass_signal_info.at(StrId("A")).insert(StrId("B"));
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagRtv});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_before_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagSrv);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(barrier_info.barrier_before_pass[StrId("C")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_after_pass  == kBufferStateFlagSrv);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("B")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagRtv});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("-"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("C")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagRtv});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagSrvNonPs});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("-"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    PassSignalInfo pass_signal_info{memory_resource.get()};
    pass_signal_info.insert({StrId("A"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
    pass_signal_info.at(StrId("A")).insert(StrId("B"));
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagSrvNonPs});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(barrier_info.barrier_before_pass[StrId("C")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("-"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvAll)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    PassSignalInfo pass_signal_info{memory_resource.get()};
    pass_signal_info.insert({StrId("A"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
    pass_signal_info.at(StrId("A")).insert(StrId("B"));
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagSrvNonPs});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("-"), BufferStateType::kSrvNonPs)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("C")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagRtv});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagSrvNonPs});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("C")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kCopyDst)}, memory_resource.get()}).CommandQueueTypeTransfer()});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("B")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagCopyDst});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_before_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagCopyDst);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer"), BufferStateType::kCopySrc)}, memory_resource.get()}).CommandQueueTypeTransfer()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("B")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagRtv});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagCopySrc);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kCopyDst)}, memory_resource.get()}).CommandQueueTypeTransfer()});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer"), BufferStateType::kSrvNonPs)}, memory_resource.get()}).CommandQueueTypeCompute()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("B")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagCopyDst});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_before_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagCopyDst);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagSrvNonPs);
    CHECK(barrier_info.barrier_before_pass[StrId("B")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer"), BufferStateType::kUav)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer"), BufferStateType::kCopySrc)}, memory_resource.get()}).CommandQueueTypeTransfer()});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    CHECK(pass_signal_info.size() == 1);
    CHECK(pass_signal_info.contains(StrId("A")));
    CHECK(pass_signal_info.at(StrId("A")).size() == 1);
    CHECK(pass_signal_info.at(StrId("A")).contains(StrId("B")));
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagUav});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagUav);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagCopySrc);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kNone);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("B")));
  }
  {
    auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
    BatchInfoList batch_info_list{memory_resource.get()};
    batch_info_list.push_back(RenderPassOrder{memory_resource.get()});
    batch_info_list.back().push_back(StrId("A"));
    batch_info_list.back().push_back(StrId("B"));
    batch_info_list.back().push_back(StrId("C"));
    batch_info_list.back().push_back(StrId("D"));
    RenderPassIdMap render_pass_id_map{memory_resource.get()};
    render_pass_id_map.insert({StrId("A"), RenderPass(StrId("A"), {{BufferConfig(StrId("buffer1"), BufferStateType::kDsv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("B"), RenderPass(StrId("B"), {{BufferConfig(StrId("buffer2"), BufferStateType::kRtv)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("C"), RenderPass(StrId("C"), {{BufferConfig(StrId("buffer1"), BufferStateType::kSrvPsOnly)}, memory_resource.get()})});
    render_pass_id_map.insert({StrId("D"), RenderPass(StrId("D"), {{BufferConfig(StrId("buffer2"), BufferStateType::kSrvAll)}, memory_resource.get()})});
    auto buffer_id_list = CreateBufferIdList(batch_info_list, render_pass_id_map, memory_resource.get());
    auto render_pass_adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, batch_info_list.back(), buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(render_pass_adjacency_graph, memory_resource.get());
    auto render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(batch_info_list), memory_resource.get());
    auto pass_signal_info = ConfigureBufferResourceDependency(render_pass_id_map, render_pass_order, consumer_producer_render_pass_map, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({0, kBufferStateFlagDsvWrite});
    buffer_state_before_render_pass_list.insert({1, kBufferStateFlagRtv});
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource.get());
    auto barrier_info = ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource.get());
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("A")));
    CHECK(barrier_info.barrier_after_pass[StrId("A")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].state_flag_after_pass  == kBufferStateFlagSrvPsOnly);
    CHECK(barrier_info.barrier_after_pass[StrId("A")][0].split_type == BarrierSplitType::kBegin);
    CHECK(!barrier_info.barrier_before_pass.contains(StrId("B")));
    CHECK(barrier_info.barrier_after_pass[StrId("B")].size() == 1);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].state_flag_after_pass  == kBufferStateFlagSrv);
    CHECK(barrier_info.barrier_after_pass[StrId("B")][0].split_type == BarrierSplitType::kBegin);
    CHECK(barrier_info.barrier_before_pass[StrId("C")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_before_pass == kBufferStateFlagDsvWrite);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].state_flag_after_pass  == kBufferStateFlagSrvPsOnly);
    CHECK(barrier_info.barrier_before_pass[StrId("C")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("C")));
    CHECK(barrier_info.barrier_before_pass[StrId("D")].size() == 1);
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].state_flag_before_pass == kBufferStateFlagRtv);
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].state_flag_after_pass  == kBufferStateFlagSrv);
    CHECK(barrier_info.barrier_before_pass[StrId("D")][0].split_type == BarrierSplitType::kEnd);
    CHECK(!barrier_info.barrier_after_pass.contains(StrId("D")));
    auto [current_frame_barriers, next_frame_barriers] = ConfigureBarrierForNextFrame(render_pass_id_map, render_pass_order, buffer_state_before_render_pass_list, {}, inter_pass_distance_map, buffer_state_change_list, memory_resource.get());
    CHECK(current_frame_barriers.barrier_before_pass.empty());
    CHECK(current_frame_barriers.barrier_after_pass.size() == 2);
    CHECK(current_frame_barriers.barrier_after_pass.contains(StrId("C")));
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("C"))[0].state_flag_before_pass == kBufferStateFlagSrvPsOnly);
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("C"))[0].state_flag_after_pass == kBufferStateFlagDsvWrite);
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("C"))[0].split_type == BarrierSplitType::kBegin);
    CHECK(current_frame_barriers.barrier_after_pass.contains(StrId("D")));
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("D"))[0].state_flag_before_pass == kBufferStateFlagSrv);
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("D"))[0].state_flag_after_pass == kBufferStateFlagRtv);
    CHECK(current_frame_barriers.barrier_after_pass.at(StrId("D"))[0].split_type == BarrierSplitType::kBegin);
    CHECK(next_frame_barriers.barrier_before_pass.size() == 2);
    CHECK(next_frame_barriers.barrier_before_pass.contains(StrId("A")));
    CHECK(next_frame_barriers.barrier_before_pass.at(StrId("A"))[0].state_flag_before_pass == kBufferStateFlagSrvPsOnly);
    CHECK(next_frame_barriers.barrier_before_pass.at(StrId("A"))[0].state_flag_after_pass == kBufferStateFlagDsvWrite);
    CHECK(next_frame_barriers.barrier_before_pass.at(StrId("A"))[0].split_type == BarrierSplitType::kEnd);
    CHECK(next_frame_barriers.barrier_before_pass.contains(StrId("B")));
    CHECK(next_frame_barriers.barrier_before_pass.at(StrId("B"))[0].state_flag_before_pass == kBufferStateFlagSrv);
    CHECK(next_frame_barriers.barrier_before_pass.at(StrId("B"))[0].state_flag_after_pass == kBufferStateFlagRtv);
    CHECK(next_frame_barriers.barrier_before_pass.at(StrId("B"))[0].split_type == BarrierSplitType::kEnd);
    CHECK(next_frame_barriers.barrier_after_pass.empty());
  }
}
TEST_CASE("MergePassSignalInfo") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  PassSignalInfo a{memory_resource.get()}, b{memory_resource.get()};
  a.insert({StrId("a"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
  a.at(StrId("a")).insert(StrId("a"));
  a.insert({StrId("c"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
  a.at(StrId("c")).insert(StrId("1"));
  a.at(StrId("c")).insert(StrId("2"));
  b.insert({StrId("b"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
  b.at(StrId("b")).insert(StrId("b"));
  b.insert({StrId("c"), std::pmr::unordered_set<StrId>{memory_resource.get()}});
  b.at(StrId("c")).insert(StrId("3"));
  b.at(StrId("c")).insert(StrId("4"));
  a = MergePassSignalInfo(std::move(a), std::move(b));
  CHECK(a.contains(StrId("a")));
  CHECK(a.contains(StrId("b")));
  CHECK(a.contains(StrId("c")));
  CHECK(a.at(StrId("a")).size() == 1);
  CHECK(a.at(StrId("a")).contains(StrId("a")));
  CHECK(a.at(StrId("b")).size() == 1);
  CHECK(a.at(StrId("b")).contains(StrId("b")));
  CHECK(a.at(StrId("c")).size() == 4);
  CHECK(a.at(StrId("c")).contains(StrId("1")));
  CHECK(a.at(StrId("c")).contains(StrId("2")));
  CHECK(a.at(StrId("c")).contains(StrId("3")));
  CHECK(a.at(StrId("c")).contains(StrId("4")));
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#endif
