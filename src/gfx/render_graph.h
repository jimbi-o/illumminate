#ifndef ILLUMINATE_RENDER_GRAPH_H
#define ILLUMINATE_RENDER_GRAPH_H
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "gfx_def.h"
#include "core/strid.h"
namespace illuminate::gfx {
enum AllowDisallow : uint8_t { kAllowed = 0, kDisallowed = 1, };
enum class ResourceStateType : uint8_t {
  kSrv = 0,
  kRtvNewBuffer,
  kRtvPrevResultReused,
  kDsvNewBuffer,
  kDsvReadWrite,
  kDsvReadOnly,
  kUavNewBuffer,
  kUavReadWrite,
  kUavReadOnly,
  kCopySrc,
  kCopyDst,
  // kUavToClear, // implement if needed
};
constexpr bool IsResourceReadable(const ResourceStateType type) {
  switch (type) {
    case ResourceStateType::kSrv:
    case ResourceStateType::kRtvPrevResultReused:
    case ResourceStateType::kDsvReadWrite:
    case ResourceStateType::kDsvReadOnly:
    case ResourceStateType::kUavReadWrite:
    case ResourceStateType::kUavReadOnly:
    case ResourceStateType::kCopySrc:
      return true;
    case ResourceStateType::kRtvNewBuffer:
    case ResourceStateType::kDsvNewBuffer:
    case ResourceStateType::kUavNewBuffer:
    case ResourceStateType::kCopyDst:
      return false;
  }
}
constexpr bool IsResourceWritable(const ResourceStateType type) {
  switch (type) {
    case ResourceStateType::kRtvNewBuffer:
    case ResourceStateType::kRtvPrevResultReused:
    case ResourceStateType::kDsvNewBuffer:
    case ResourceStateType::kDsvReadWrite:
    case ResourceStateType::kUavNewBuffer:
    case ResourceStateType::kUavReadWrite:
    case ResourceStateType::kCopyDst:
      return true;
    case ResourceStateType::kSrv:
    case ResourceStateType::kDsvReadOnly:
    case ResourceStateType::kUavReadOnly:
    case ResourceStateType::kCopySrc:
      return false;
  }
}
using AsyncComputeAllowed = AllowDisallow;
template <typename RenderFunction>
struct RenderGraphPass {
  std::unordered_map<ResourceStateType, std::vector<StrId>> resources;
  RenderFunction render_function;
  CommandQueueType command_queue_type;
  AsyncComputeAllowed async_compute_allowed;
  [[maybe_unused]] uint8_t _dmy[2];
};
template <typename RenderFunction>
struct RenderGraphBatch {
  std::vector<RenderGraphPass<RenderFunction>> pass;
};
template <typename RenderFunction>
struct RenderGraphConfig {
  std::vector<RenderGraphBatch<RenderFunction>> batch;
};
struct BarrierInfo {
};
using PassId = uint32_t;
using BatchId = uint32_t;
using ResourceId = uint32_t;
template <typename RenderFunction>
struct ParsedRenderGraph {
  std::vector<BatchId> batch_order;
  std::unordered_map<BatchId, std::vector<std::tuple<CommandQueueType, uint32_t>>> batch_command_list_num;
  std::unordered_map<BatchId/*producer*/, std::unordered_set<CommandQueueType/*producer*/>> need_signal_queue_batch;
  std::unordered_map<BatchId/*consumer*/, std::vector<std::tuple<CommandQueueType/*producer*/, BatchId/*producer*/, CommandQueueType/*consumer*/>>> batch_wait_queue_info;
  std::unordered_map<BatchId, std::vector<PassId>> batched_pass_order;
  std::unordered_map<PassId, CommandQueueType> pass_command_queue_type;
  std::unordered_map<PassId, std::vector<BarrierInfo>> pre_pass_barriers;
  std::unordered_map<PassId, uint32_t> pass_command_list_index;
  std::unordered_map<PassId, RenderFunction> pass_render_function;
  std::unordered_map<BatchId, std::unordered_map<CommandQueueType, std::vector<BarrierInfo>>> post_batch_barriers;
  std::unordered_map<BatchId, std::unordered_map<CommandQueueType, uint32_t>> post_batch_barriers_command_list_index;
  CommandQueueType frame_end_signal_queue;
};
template <typename RenderFunction>
auto ConfigureBatchedPassList(RenderGraphConfig<RenderFunction>&& config) {
  std::vector<BatchId> batch_order;
  batch_order.reserve(batch_order.size());
  std::unordered_map<BatchId, std::vector<PassId>> batched_pass_order;
  batched_pass_order.reserve(batch_order.size());
  std::unordered_map<PassId, RenderGraphPass<RenderFunction>> pass_list;
  uint32_t pass_id = 0;
  for (uint32_t batch_id = 0; auto&& batch : config.batch) {
    batch_order.push_back(batch_id);
    batched_pass_order[batch_id] = {};
    for (auto&& pass : batch.pass) {
      batched_pass_order.at(batch_id).push_back(pass_id);
      pass_list[pass_id] = std::move(pass);
      pass_id++;
    }
    batch_id++;
  }
  return std::make_tuple(batch_order, batched_pass_order, pass_list);
}
template <typename RenderFunction>
auto IdentifyPassResources(const std::vector<BatchId>& batch_order, const std::unordered_map<BatchId, std::vector<PassId>>& batched_pass_order, const std::unordered_map<PassId, RenderGraphPass<RenderFunction>>& pass_list) {
  std::unordered_map<PassId, std::unordered_map<ResourceStateType, std::vector<ResourceId>>> pass_binded_resource_id_list;
  ResourceId next_id = 0;
  std::unordered_map<StrId, ResourceId> touched_resources;
  for (auto& batch_id : batch_order) {
    auto& pass_id_list = batched_pass_order.at(batch_id);
    for (auto& pass_id : pass_id_list) {
      auto& pass = pass_list.at(pass_id);
      pass_binded_resource_id_list[pass_id].reserve(pass.resources.size());
      for (auto& [resource_type, resources] : pass.resources) {
        pass_binded_resource_id_list.at(pass_id)[resource_type].reserve(resources.size());
        auto write_only = !IsResourceReadable(resource_type) && IsResourceWritable(resource_type);
        for (auto& resource_name : resources) {
          auto need_new_id = write_only || !touched_resources.contains(resource_name);
          if (need_new_id) {
            touched_resources[resource_name] = next_id;
            next_id++;
          }
          pass_binded_resource_id_list.at(pass_id).at(resource_type).push_back(touched_resources.at(resource_name));
        }
      }
    }
  }
  return pass_binded_resource_id_list;
}
template <typename RenderFunction>
auto ParseRenderGraph(RenderGraphConfig<RenderFunction>&& render_graph_config) {
  (void) render_graph_config;
  ParsedRenderGraph<RenderFunction> parsed_graph;
  // TODO
  return parsed_graph;
}
}
#endif
