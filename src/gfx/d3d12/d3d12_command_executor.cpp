#include "d3d12_minimal_for_cpp.h"
#include <ranges>
// TODO move to upper layer directory.
namespace illuminate::gfx {
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
template <typename RenderFunction>
struct RenderGraphPass {
  CommandQueueType command_queue_type;
  RenderFunction render_function;
  std::unordered_map<ResourceStateType, std::vector<StrId>> resources;
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
auto CorrectConsumerProducerInSameBatchDifferentQueue(std::vector<BatchId>&& batch_order, std::unordered_map<BatchId, std::vector<PassId>>&& batched_pass_order, const std::unordered_map<PassId, RenderGraphPass<RenderFunction>>& pass_list) {
  // TODO
  return std::make_tuple(batch_order, batched_pass_order);
}
template <typename RenderFunction>
auto GetPassInputResourceList(const RenderGraphPass<RenderFunction>& pass) {
  std::vector<StrId> input_resource_names;
  for (auto& [type, vec] : pass.resources) {
    if (!IsResourceReadable(type)) continue;
    input_resource_names.reserve(input_resource_names.size() + vec.size());
    input_resource_names.insert(input_resource_names.end(), vec.begin(), vec.end());
  }
  return input_resource_names;
}
template <typename InnerLoopFunction>
auto IterateBatchedPassListBackward(const std::vector<BatchId>& batch_order, const std::unordered_map<BatchId, std::vector<PassId>>& batched_pass_order, std::vector<BatchId>::const_reverse_iterator batch_it_begin, std::vector<PassId>::const_reverse_iterator pass_it_begin, InnerLoopFunction&& inner_loop_func) {
  for (auto batch_it = batch_it_begin; batch_it != batch_order.crend(); batch_it++) {
    auto& pass_id_list = batched_pass_order.at(*batch_it);
    for (auto pass_it = (batch_it == batch_it_begin) ? pass_it_begin : pass_id_list.crbegin(); pass_it != pass_id_list.crend(); pass_it++) {
      if (inner_loop_func(batch_it, pass_it)) return std::make_tuple(batch_it, pass_it);
    }
  }
  return std::make_tuple(batch_order.crend(), std::vector<PassId>::const_reverse_iterator{});
}
static auto GetResourceConsumerPassIteratorsBackward(const std::vector<BatchId>& batch_order, const std::unordered_map<BatchId, std::vector<PassId>>& batched_pass_order, const PassId resource_consumer_pass) {
  return IterateBatchedPassListBackward(batch_order, batched_pass_order, batch_order.crbegin(), batched_pass_order.at(*batch_order.crbegin()).crbegin(), [=](auto, auto pass_it){
      return *pass_it == resource_consumer_pass;
    });
}
template <typename RenderFunction>
auto GetResourceProducerPassList(const std::vector<BatchId>& batch_order, const std::unordered_map<BatchId, std::vector<PassId>>& batched_pass_order, const std::unordered_map<PassId, RenderGraphPass<RenderFunction>>& pass_list, const PassId resource_consumer_pass, const StrId resource_name) {
  std::vector<PassId> producer_pass_ids;
  if (batched_pass_order.empty()) return producer_pass_ids;
  auto [resource_consumer_batch_it, resource_consumer_pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, resource_consumer_pass);
  auto resource_consumer_batch = *resource_consumer_batch_it;
  CommandQueueType resource_consumer_pass_queue_type = pass_list.at(*resource_consumer_pass_it).command_queue_type;
  IterateBatchedPassListBackward(batch_order, batched_pass_order, resource_consumer_batch_it, resource_consumer_pass_it + 1, [&](auto batch_it, auto pass_it) {
    auto& pass_id = *pass_it;
    auto& pass = pass_list.at(pass_id);
    bool stop_searching = false;
    for (auto& [type, vec] : pass.resources) {
      if (!IsResourceWritable(type)) continue;
      if (*batch_it == resource_consumer_batch && pass.command_queue_type != resource_consumer_pass_queue_type) continue;
      bool resource_found = false;
      for (auto& name : vec) {
        if (name != resource_name) continue;
        resource_found = true;
        stop_searching = !IsResourceReadable(type);
        producer_pass_ids.push_back(pass_id);
        break;
      }
      if (resource_found) break;
    }
    return stop_searching;
  });
  return producer_pass_ids;
}
template <typename RenderFunction>
auto CreateConsumerProducerPassAdjacancyList(const std::vector<BatchId>& batch_order, const std::unordered_map<BatchId, std::vector<PassId>>& batched_pass_order, const std::unordered_map<PassId, RenderGraphPass<RenderFunction>>& pass_list) {
  std::unordered_map<PassId, std::unordered_set<PassId>> consumer_producer_pass_adjacancy_list;
  consumer_producer_pass_adjacancy_list.reserve(pass_list.size());
  for (auto& [pass_id, pass] : pass_list) {
    auto input_resources = GetPassInputResourceList(pass);
    consumer_producer_pass_adjacancy_list[pass_id] = {};
    for (auto& buffer_name : input_resources) {
      auto producer_pass = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, pass_id, buffer_name);
      if (producer_pass.empty()) continue;
      consumer_producer_pass_adjacancy_list.at(pass_id).insert(producer_pass.begin(), producer_pass.end());
    }
  }
  return consumer_producer_pass_adjacancy_list;
}
template <typename RenderFunction>
auto ParseRenderGraph(RenderGraphConfig<RenderFunction>&& render_graph_config) {
  ParsedRenderGraph<RenderFunction> parsed_graph;
  // TODO
  return parsed_graph;
}
}
#include <queue>
namespace illuminate::gfx::d3d12 {
struct PhysicalResources {
  std::unordered_map<PassId, D3D12_GPU_DESCRIPTOR_HANDLE> cbv_srv_uav;
  std::unordered_map<PassId, D3D12_CPU_DESCRIPTOR_HANDLE> rtv;
  std::unordered_map<PassId, D3D12_CPU_DESCRIPTOR_HANDLE> dsv;
  std::unordered_map<PassId, std::vector<ID3D12Resource*>> copy_src;
  std::unordered_map<PassId, std::vector<ID3D12Resource*>> copy_dst;
  std::unordered_map<PassId, std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>> uav_to_clear_gpu_handle;
  std::unordered_map<PassId, std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>> uav_to_clear_cpu_handle;
  std::unordered_map<PassId, std::vector<ID3D12Resource*>> uav_to_clear_resource;
  std::unordered_map<PassId, std::vector<ID3D12Resource*>> barrier_resources_pre_render_pass;
  std::unordered_map<BatchId, std::unordered_map<CommandQueueType, std::vector<ID3D12Resource*>>> barrier_resources_post_batch;
};
using RenderFunction = std::function<void(D3d12CommandList**, const PhysicalResources&, const uint32_t/*pass_id*/)>;
using RenderGraphConfigD3d12 = RenderGraphConfig<RenderFunction>;
static auto PreparePhysicalResource(const ParsedRenderGraph<RenderFunction>& render_graph) {
  PhysicalResources physical_resources;
  // TODO
  return physical_resources;
}
static auto ExecuteResourceBarriers(D3d12CommandList* command_list, const std::vector<BarrierInfo>& barriers, const std::vector<ID3D12Resource*>& resources) {
  // TODO
}
}
namespace {
using namespace illuminate::gfx;
using namespace illuminate::gfx::d3d12;
static auto GetRenderGraphSimple() {
  RenderGraphConfigD3d12 config{};
  // TODO
  return config;
}
using CreateRenderGraphFunc = std::function<RenderGraphConfigD3d12()>;
}
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "gfx/win32/win32_window.h"
#include "d3d12_swapchain.h"
TEST_CASE("batch-pass") {
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  {
    RenderGraphConfig<void*> config;
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    CHECK(batch_order.empty());
    CHECK(batched_pass_order.empty());
    CHECK(pass_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    CHECK(batch_order.size() == 1);
    CHECK(batched_pass_order.size() == 1);
    CHECK(batched_pass_order[0].size() == 1);
    CHECK(batched_pass_order[0][0] == 0);
    CHECK(pass_list.size() == 1);
    CHECK(pass_list[0].render_function == 1);
    CHECK(pass_list[0].command_queue_type == CommandQueueType::kGraphics);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {}},
          {CommandQueueType::kCompute,  2, {}},
        }},
        {{
          {CommandQueueType::kCompute,  4, {}},
          {CommandQueueType::kGraphics, 3, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    CHECK(batch_order.size() == 2);
    CHECK(batched_pass_order.size() == 2);
    CHECK(batched_pass_order[0].size() == 2);
    CHECK(batched_pass_order[0][0] == 0);
    CHECK(batched_pass_order[0][1] == 1);
    CHECK(batched_pass_order[1][0] == 2);
    CHECK(batched_pass_order[1][1] == 3);
    CHECK(pass_list.size() == 4);
    CHECK(pass_list[0].render_function == 1);
    CHECK(pass_list[1].render_function == 2);
    CHECK(pass_list[2].render_function == 4);
    CHECK(pass_list[3].render_function == 3);
    CHECK(pass_list[0].command_queue_type == CommandQueueType::kGraphics);
    CHECK(pass_list[1].command_queue_type == CommandQueueType::kCompute);
    CHECK(pass_list[2].command_queue_type == CommandQueueType::kCompute);
    CHECK(pass_list[3].command_queue_type == CommandQueueType::kGraphics);
  }
}
TEST_CASE("get pass list writing to a specified buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  {
    RenderGraphPass<uint32_t> pass{
    };
    auto input_list = GetPassInputResourceList(pass);
    CHECK(input_list.empty());
  }
  {
    RenderGraphPass<uint32_t> pass{
      CommandQueueType::kGraphics, 0,
      {
        {ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyB"), StrId("dmyC"), }},
      },
    };
    auto input_list = GetPassInputResourceList(pass);
    CHECK(input_list.empty());
  }
  {
    RenderGraphPass<uint32_t> pass{
      CommandQueueType::kGraphics, 0,
      {
        {ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyB"), StrId("dmyC"), }},
        {ResourceStateType::kRtvPrevResultReused, {StrId("dmyD"), StrId("dmyE"), StrId("dmyF"), }},
        {ResourceStateType::kSrv, {StrId("dmyG"), }},
      },
    };
    auto input_list = GetPassInputResourceList(pass);
    CHECK(input_list.size() == 4);
    CHECK(IsContaining(input_list, StrId("dmyD")));
    CHECK(IsContaining(input_list, StrId("dmyE")));
    CHECK(IsContaining(input_list, StrId("dmyF")));
    CHECK(IsContaining(input_list, StrId("dmyG")));
  }
  {
    RenderGraphPass<uint32_t> pass{
      CommandQueueType::kGraphics, 0,
      {
        {ResourceStateType::kSrv, {StrId("dmyA"), StrId("dmyB"), }},
        {ResourceStateType::kRtvNewBuffer, {StrId("dmyC"), StrId("dmyD"), StrId("dmyE"), }},
        {ResourceStateType::kRtvPrevResultReused, {StrId("dmyF"), StrId("dmyG"), }},
        {ResourceStateType::kDsvNewBuffer, {StrId("dmyH"), StrId("dmyI"), }},
        {ResourceStateType::kDsvReadWrite, {StrId("dmyJ"), StrId("dmyK"), }},
        {ResourceStateType::kDsvReadOnly, {StrId("dmyL"), StrId("dmyM"), StrId("dmyN"), }},
        {ResourceStateType::kUavNewBuffer, {StrId("dmyO"), StrId("dmyP"), }},
        {ResourceStateType::kUavReadWrite, {StrId("dmyQ"), StrId("dmyR"), }},
        {ResourceStateType::kCopySrc, {StrId("dmyS"), StrId("dmyT"), }},
        {ResourceStateType::kCopyDst, {StrId("dmyU"), StrId("dmyV"), StrId("dmyW"), }},
      },
    };
    auto input_list = GetPassInputResourceList(pass);
    CHECK(input_list.size() == 13);
    CHECK(IsContaining(input_list, StrId("dmyA")));
    CHECK(IsContaining(input_list, StrId("dmyB")));
    CHECK(IsContaining(input_list, StrId("dmyF")));
    CHECK(IsContaining(input_list, StrId("dmyG")));
    CHECK(IsContaining(input_list, StrId("dmyJ")));
    CHECK(IsContaining(input_list, StrId("dmyK")));
    CHECK(IsContaining(input_list, StrId("dmyL")));
    CHECK(IsContaining(input_list, StrId("dmyM")));
    CHECK(IsContaining(input_list, StrId("dmyN")));
    CHECK(IsContaining(input_list, StrId("dmyQ")));
    CHECK(IsContaining(input_list, StrId("dmyR")));
    CHECK(IsContaining(input_list, StrId("dmyS")));
    CHECK(IsContaining(input_list, StrId("dmyT")));
  }
  {
    RenderGraphConfig<uint32_t> config;
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, 0, StrId("swapchain"));
    CHECK(resource_producer_pass_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 0);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 0);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("swapchain"));
    CHECK(resource_producer_pass_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {}},
          {CommandQueueType::kCompute,  2, {}},
        }},
        {{
          {CommandQueueType::kCompute,  4, {}},
          {CommandQueueType::kGraphics, 3, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 3);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 3);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("swapchain"));
    CHECK(resource_producer_pass_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    SUBCASE("buffer exists") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 1);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 1);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 1);
      CHECK(IsContaining(resource_producer_pass_list, 0));
    }
    SUBCASE("buffer does not exist") {
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("swapchain"));
      CHECK(resource_producer_pass_list.empty());
    }
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 2);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 2);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
    CHECK(resource_producer_pass_list.size() == 2);
    CHECK(IsContaining(resource_producer_pass_list, 0));
    CHECK(IsContaining(resource_producer_pass_list, 1));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    SUBCASE("search from back") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 3);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 3);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 2);
      CHECK(IsContaining(resource_producer_pass_list, 1));
      CHECK(IsContaining(resource_producer_pass_list, 2));
    }
    SUBCASE("search from in middle") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 2);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 2);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, 2, StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 1);
      CHECK(IsContaining(resource_producer_pass_list, 1));
    }
    SUBCASE("search from in middle2") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 1);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 1);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, 1, StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 1);
      CHECK(IsContaining(resource_producer_pass_list, 0));
    }
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 3);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 3);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
    CHECK(resource_producer_pass_list.size() == 3);
    CHECK(IsContaining(resource_producer_pass_list, 0));
    CHECK(IsContaining(resource_producer_pass_list, 1));
    CHECK(IsContaining(resource_producer_pass_list, 2));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmy"), StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmy"), StrId("mainbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    SUBCASE("search from back") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 4);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 4);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 2);
      CHECK(IsContaining(resource_producer_pass_list, 2));
      CHECK(IsContaining(resource_producer_pass_list, 3));
    }
    SUBCASE("search from in middle") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 3);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 3);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, 3, StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 1);
      CHECK(IsContaining(resource_producer_pass_list, 2));
    }
    SUBCASE("search from in middle 2") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 2);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 2);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, 2, StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 2);
      CHECK(IsContaining(resource_producer_pass_list, 0));
      CHECK(IsContaining(resource_producer_pass_list, 1));
    }
    SUBCASE("search from in middle 3") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 1);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 1);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, 1, StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.size() == 1);
      CHECK(IsContaining(resource_producer_pass_list, 0));
    }
    SUBCASE("search from head") {
      auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 0);
      CHECK(batch_it != batch_order.crend());
      CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
      CHECK(*pass_it == 0);
      auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, 0, StrId("mainbuffer"));
      CHECK(resource_producer_pass_list.empty());
    }
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvNewBuffer, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadWrite, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadOnly, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 3);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 3);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("depth"));
    CHECK(resource_producer_pass_list.size() == 2);
    CHECK(IsContaining(resource_producer_pass_list, 0));
    CHECK(IsContaining(resource_producer_pass_list, 1));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvNewBuffer, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadWrite, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadOnly, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 4);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 4);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("depth"));
    CHECK(resource_producer_pass_list.size() == 2);
    CHECK(IsContaining(resource_producer_pass_list, 1));
    CHECK(IsContaining(resource_producer_pass_list, 2));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavNewBuffer, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadOnly, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 4);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 4);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(resource_producer_pass_list.size() == 2);
    CHECK(IsContaining(resource_producer_pass_list, 1));
    CHECK(IsContaining(resource_producer_pass_list, 2));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kCopyDst, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadOnly, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 4);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 4);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(resource_producer_pass_list.size() == 2);
    CHECK(IsContaining(resource_producer_pass_list, 1));
    CHECK(IsContaining(resource_producer_pass_list, 2));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kCompute, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 2);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 2);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(resource_producer_pass_list.size() == 1);
    CHECK(IsContaining(resource_producer_pass_list, 1));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 2);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 2);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(resource_producer_pass_list.size() == 1);
    CHECK(IsContaining(resource_producer_pass_list, 0));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
        }},
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kCompute, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 2);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 2);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(resource_producer_pass_list.size() == 2);
    CHECK(IsContaining(resource_producer_pass_list, 0));
    CHECK(IsContaining(resource_producer_pass_list, 1));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto [batch_it, pass_it] = GetResourceConsumerPassIteratorsBackward(batch_order, batched_pass_order, 2);
    CHECK(batch_it != batch_order.crend());
    CHECK(pass_it != batched_pass_order.at(*batch_it).crend());
    CHECK(*pass_it == 2);
    auto resource_producer_pass_list = GetResourceProducerPassList(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(resource_producer_pass_list.size() == 2);
    CHECK(IsContaining(resource_producer_pass_list, 0));
    CHECK(IsContaining(resource_producer_pass_list, 1));
  }
}
TEST_CASE("create adjacancy list") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  {
    RenderGraphConfig<uint32_t> config;
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto consumer_producer_pass_adjacancy_list = CreateConsumerProducerPassAdjacancyList(batch_order, batched_pass_order, pass_list);
    CHECK(consumer_producer_pass_adjacancy_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto consumer_producer_pass_adjacancy_list = CreateConsumerProducerPassAdjacancyList(batch_order, batched_pass_order, pass_list);
    CHECK(consumer_producer_pass_adjacancy_list.size() == 1);
    CHECK(consumer_producer_pass_adjacancy_list.at(0).empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kSrv, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto consumer_producer_pass_adjacancy_list = CreateConsumerProducerPassAdjacancyList(batch_order, batched_pass_order, pass_list);
    CHECK(consumer_producer_pass_adjacancy_list.size() == 3);
    CHECK(consumer_producer_pass_adjacancy_list.at(0).empty());
    CHECK(consumer_producer_pass_adjacancy_list.at(1).size() == 1);
    CHECK(consumer_producer_pass_adjacancy_list.at(1).contains(0));
    CHECK(consumer_producer_pass_adjacancy_list.at(2).size() == 2);
    CHECK(consumer_producer_pass_adjacancy_list.at(2).contains(0));
    CHECK(consumer_producer_pass_adjacancy_list.at(2).contains(1));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("subbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kSrv, {StrId("mainbuffer"), StrId("subbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto consumer_producer_pass_adjacancy_list = CreateConsumerProducerPassAdjacancyList(batch_order, batched_pass_order, pass_list);
    CHECK(consumer_producer_pass_adjacancy_list.size() == 5);
    CHECK(consumer_producer_pass_adjacancy_list.at(0).empty());
    CHECK(consumer_producer_pass_adjacancy_list.at(1).empty());
    CHECK(consumer_producer_pass_adjacancy_list.contains(2));
    CHECK(consumer_producer_pass_adjacancy_list.at(2).size() == 1);
    CHECK(consumer_producer_pass_adjacancy_list.at(2).contains(1));
    CHECK(consumer_producer_pass_adjacancy_list.contains(3));
    CHECK(consumer_producer_pass_adjacancy_list.at(3).size() == 1);
    CHECK(consumer_producer_pass_adjacancy_list.at(3).contains(0));
    CHECK(consumer_producer_pass_adjacancy_list.contains(4));
    CHECK(consumer_producer_pass_adjacancy_list.at(4).size() == 4);
    CHECK(consumer_producer_pass_adjacancy_list.at(4).contains(0));
    CHECK(consumer_producer_pass_adjacancy_list.at(4).contains(1));
    CHECK(consumer_producer_pass_adjacancy_list.at(4).contains(2));
    CHECK(consumer_producer_pass_adjacancy_list.at(4).contains(3));
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("subbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("subbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kSrv, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kSrv, {StrId("subbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto consumer_producer_pass_adjacancy_list = CreateConsumerProducerPassAdjacancyList(batch_order, batched_pass_order, pass_list);
    CHECK(consumer_producer_pass_adjacancy_list.size() == 6);
    CHECK(consumer_producer_pass_adjacancy_list.at(0).empty());
    CHECK(consumer_producer_pass_adjacancy_list.at(1).empty());
    CHECK(consumer_producer_pass_adjacancy_list.contains(2));
    CHECK(consumer_producer_pass_adjacancy_list.contains(3));
    CHECK(consumer_producer_pass_adjacancy_list.contains(4));
    CHECK(consumer_producer_pass_adjacancy_list.contains(5));
    CHECK(consumer_producer_pass_adjacancy_list.at(2).size() == 1);
    CHECK(consumer_producer_pass_adjacancy_list.at(2).contains(1));
    CHECK(consumer_producer_pass_adjacancy_list.at(3).size() == 1);
    CHECK(consumer_producer_pass_adjacancy_list.at(3).contains(0));
    CHECK(consumer_producer_pass_adjacancy_list.at(4).size() == 2);
    CHECK(consumer_producer_pass_adjacancy_list.at(4).contains(1));
    CHECK(consumer_producer_pass_adjacancy_list.at(4).contains(2));
    CHECK(consumer_producer_pass_adjacancy_list.at(5).size() == 2);
    CHECK(consumer_producer_pass_adjacancy_list.at(5).contains(0));
    CHECK(consumer_producer_pass_adjacancy_list.at(5).contains(3));
  }
}
TEST_CASE("validate render graph config") {
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("subbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 1);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
    CHECK(batched_pass_order.at(1)[1] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(1).size() == 1);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
    CHECK(batched_pass_order.at(1)[1] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 2);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
    CHECK(batched_pass_order.at(1)[1] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("subbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 3);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(1)[0] == 3);
    CHECK(batched_pass_order.at(1)[1] == 4);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 4);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 1);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 1);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(1)[0] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 3);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 2);
    CHECK(batch_order[2] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(2).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 2);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(2)[0] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
    CHECK(batched_pass_order.at(1)[1] == 3);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyA")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("subbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
        }},
        {{
            {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyC")}}, {ResourceStateType::kUavReadWrite, {StrId("subbuffer")}}}},
            {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyB")}}}},
            {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("subbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 3);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 2);
    CHECK(batch_order[2] == 1);
    CHECK(batched_pass_order.at(0).size() == 1);
    CHECK(batched_pass_order.at(2).size() == 1);
    CHECK(batched_pass_order.at(1).size() == 3);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(2)[0] == 1);
    CHECK(batched_pass_order.at(1)[0] == 2);
    CHECK(batched_pass_order.at(1)[1] == 3);
    CHECK(batched_pass_order.at(1)[2] == 4);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 4);
    CHECK(batched_pass_order.at(1).size() == 3);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(1)[0] == 4);
    CHECK(batched_pass_order.at(1)[1] == 5);
    CHECK(batched_pass_order.at(1)[2] == 6);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batch_order[0] == 0);
    CHECK(batched_pass_order.at(0).size() == 7);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(0)[4] == 4);
    CHECK(batched_pass_order.at(0)[5] == 5);
    CHECK(batched_pass_order.at(0)[6] == 6);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batched_pass_order.at(0).size() == 8);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(0)[4] == 4);
    CHECK(batched_pass_order.at(0)[5] == 5);
    CHECK(batched_pass_order.at(0)[6] == 6);
    CHECK(batched_pass_order.at(0)[7] == 7);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyA"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyE")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer"), StrId("dmyF")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 1);
    CHECK(batched_pass_order.at(0).size() == 8);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(0)[4] == 4);
    CHECK(batched_pass_order.at(0)[5] == 5);
    CHECK(batched_pass_order.at(0)[6] == 6);
    CHECK(batched_pass_order.at(0)[7] == 7);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("cbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("dbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("subbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}, {ResourceStateType::kUavReadWrite, {StrId("cbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
    CHECK(batch_order.size() == 2);
    CHECK(batch_order[0] == 0);
    CHECK(batch_order[1] == 1);
    CHECK(batched_pass_order.at(0).size() == 4);
    CHECK(batched_pass_order.at(1).size() == 4);
    CHECK(batched_pass_order.at(0)[0] == 0);
    CHECK(batched_pass_order.at(0)[1] == 1);
    CHECK(batched_pass_order.at(0)[2] == 2);
    CHECK(batched_pass_order.at(0)[3] == 3);
    CHECK(batched_pass_order.at(1)[0] == 4);
    CHECK(batched_pass_order.at(1)[1] == 5);
    CHECK(batched_pass_order.at(1)[2] == 6);
    CHECK(batched_pass_order.at(1)[3] == 7);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("cbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("dbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("subbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer"), StrId("dmyF")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyG"), StrId("dmyH")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dmyI")}}, {ResourceStateType::kUavReadWrite, {StrId("cbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("cbuffer")}}, {ResourceStateType::kUavReadWrite, {StrId("mainbuffer"), StrId("dbuffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("subbuffer"), StrId("dmyC")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("dmyB"), StrId("dmyD")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavNewBuffer, {StrId("dbuffer"), StrId("cbuffer")}}}},
        }},
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    SUBCASE("sequential id") {
      std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
      CHECK(batch_order.size() == 4);
      CHECK(batch_order[0] == 0);
      CHECK(batch_order[1] == 3);
      CHECK(batch_order[2] == 1);
      CHECK(batch_order[3] == 2);
      CHECK(batched_pass_order.at(0).size() == 4);
      CHECK(batched_pass_order.at(3).size() == 4);
      CHECK(batched_pass_order.at(1).size() == 4);
      CHECK(batched_pass_order.at(2).size() == 3);
      CHECK(batched_pass_order.at(0)[0] == 0);
      CHECK(batched_pass_order.at(0)[1] == 1);
      CHECK(batched_pass_order.at(0)[2] == 2);
      CHECK(batched_pass_order.at(0)[3] == 3);
      CHECK(batched_pass_order.at(3)[0] == 4);
      CHECK(batched_pass_order.at(3)[1] == 5);
      CHECK(batched_pass_order.at(3)[2] == 6);
      CHECK(batched_pass_order.at(3)[3] == 7);
      CHECK(batched_pass_order.at(1)[0] == 8);
      CHECK(batched_pass_order.at(1)[1] == 9);
      CHECK(batched_pass_order.at(1)[2] == 10);
      CHECK(batched_pass_order.at(1)[3] == 11);
      CHECK(batched_pass_order.at(2)[0] == 12);
      CHECK(batched_pass_order.at(2)[1] == 13);
      CHECK(batched_pass_order.at(2)[2] == 14);
    }
    SUBCASE("non-sequential id") {
      batch_order.push_back(10);
      batch_order.push_back(9);
      batched_pass_order[10] = {};
      batched_pass_order[9] = {};
      std::tie(batch_order, batched_pass_order) = CorrectConsumerProducerInSameBatchDifferentQueue(std::move(batch_order), std::move(batched_pass_order), pass_list);
      CHECK(batch_order.size() == 6);
      CHECK(batch_order[0] == 0);
      CHECK(batch_order[1] == 11);
      CHECK(batch_order[2] == 1);
      CHECK(batch_order[3] == 2);
      CHECK(batch_order[4] == 10);
      CHECK(batch_order[5] == 9);
      CHECK(batched_pass_order.at(0).size() == 4);
      CHECK(batched_pass_order.at(11).size() == 4);
      CHECK(batched_pass_order.at(1).size() == 4);
      CHECK(batched_pass_order.at(2).size() == 3);
      CHECK(batched_pass_order.at(9).empty());
      CHECK(batched_pass_order.at(10).empty());
      CHECK(batched_pass_order.at(0)[0] == 0);
      CHECK(batched_pass_order.at(0)[1] == 1);
      CHECK(batched_pass_order.at(0)[2] == 2);
      CHECK(batched_pass_order.at(0)[3] == 3);
      CHECK(batched_pass_order.at(11)[0] == 4);
      CHECK(batched_pass_order.at(11)[1] == 5);
      CHECK(batched_pass_order.at(11)[2] == 6);
      CHECK(batched_pass_order.at(11)[3] == 7);
      CHECK(batched_pass_order.at(1)[0] == 8);
      CHECK(batched_pass_order.at(1)[1] == 9);
      CHECK(batched_pass_order.at(1)[2] == 10);
      CHECK(batched_pass_order.at(1)[3] == 11);
      CHECK(batched_pass_order.at(2)[0] == 12);
      CHECK(batched_pass_order.at(2)[1] == 13);
      CHECK(batched_pass_order.at(2)[2] == 14);
    }
  }
}
TEST_CASE("execute command list") {
  const uint32_t buffer_num = 2;
  const uint32_t swapchain_buffer_num = buffer_num + 1;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  CommandQueue command_queue;
  CHECK(command_queue.Init(device.Get()));
  illuminate::gfx::win32::Window window;
  CHECK(window.Init("swapchain test", 160, 90));
  Swapchain swapchain;
  CHECK(swapchain.Init(dxgi_core.GetFactory(), command_queue.Get(CommandQueueType::kGraphics), device.Get(), window.GetHwnd(), swapchain_buffer_num, buffer_num));
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.Get()));
  CommandList command_list;
  CHECK(command_list.Init(device.Get()));
  std::vector<std::vector<ID3D12CommandAllocator**>> allocators(buffer_num);
  std::vector<std::tuple<CommandQueueType, uint64_t>> queue_signal_val(buffer_num);
  std::unordered_map<CommandQueueType, uint64_t> used_signal_val;
  CreateRenderGraphFunc create_render_graph_func;
  SUBCASE("simple graph") {
    create_render_graph_func = GetRenderGraphSimple;
  }
  for (uint32_t i = 0; i < 10 * buffer_num; i++) {
    CAPTURE(i);
    if (const auto [queue_type, val] = queue_signal_val[i % buffer_num]; val > 0) {
      command_queue.WaitOnCpu({{queue_type, val}});
    }
    for (auto a : allocators.front()) {
      command_allocator.ReturnCommandAllocator(a);
    }
    allocators.front().clear();
    std::rotate(allocators.begin(), allocators.begin() + 1, allocators.end());
    swapchain.UpdateBackBufferIndex();
    auto parsed_render_graph = ParseRenderGraph(create_render_graph_func());
    auto physical_resources = PreparePhysicalResource(parsed_render_graph);
    for (std::unordered_map<BatchId, std::unordered_map<CommandQueueType, uint64_t>> batch_signaled_val; auto batch_id : parsed_render_graph.batch_order) {
      std::unordered_map<CommandQueueType, D3d12CommandList**> command_lists;
      for (auto& [queue_type, command_list_num] : parsed_render_graph.batch_command_list_num.at(batch_id)) {
        auto command_allocators = command_allocator.RetainCommandAllocator(queue_type, command_list_num);
        command_lists[queue_type] = command_list.RetainCommandList(queue_type, command_list_num, command_allocators);
        allocators.back().push_back(std::move(command_allocators));
      }
      if (parsed_render_graph.batch_wait_queue_info.contains(batch_id)) {
        for (auto& [wait_queue, signaled_batch, signaled_queue] : parsed_render_graph.batch_wait_queue_info.at(batch_id)) {
          command_queue.RegisterWaitOnQueue(signaled_queue, batch_signaled_val.at(signaled_batch).at(signaled_queue), wait_queue);
        }
      }
      for (auto pass_id : parsed_render_graph.batched_pass_order.at(batch_id)) {
        auto queue_type = parsed_render_graph.pass_command_queue_type.at(pass_id);
        auto command_list_index = parsed_render_graph.pass_command_list_index.at(pass_id);
        if (parsed_render_graph.pre_pass_barriers.contains(pass_id)) {
          ExecuteResourceBarriers(command_lists.at(queue_type)[command_list_index], parsed_render_graph.pre_pass_barriers.at(pass_id), physical_resources.barrier_resources_pre_render_pass.at(pass_id));
        }
        parsed_render_graph.pass_render_function.at(pass_id)(&command_lists.at(queue_type)[command_list_index], physical_resources, pass_id);
      }
      if (parsed_render_graph.post_batch_barriers_command_list_index.contains(batch_id)) {
        for (auto& [queue_type, command_list_index] : parsed_render_graph.post_batch_barriers_command_list_index.at(batch_id)) {
          ExecuteResourceBarriers(command_lists.at(queue_type)[command_list_index], parsed_render_graph.post_batch_barriers.at(batch_id).at(queue_type), physical_resources.barrier_resources_post_batch.at(batch_id).at(queue_type));
        }
      }
      for (auto& [queue_type, command_list_num] : parsed_render_graph.batch_command_list_num.at(batch_id)) {
        for (uint32_t j = 0; j < command_list_num; j++) {
          auto hr = command_lists.at(queue_type)[j]->Close();
          if (FAILED(hr)) {
            logwarn("close command list failed. {}", hr);
            continue;
          }
        }
        auto command_lists_to_execute = command_lists.at(queue_type);
        command_queue.Get(queue_type)->ExecuteCommandLists(command_list_num, reinterpret_cast<ID3D12CommandList**>(command_lists_to_execute));
        command_list.ReturnCommandList(command_lists_to_execute);
        if (parsed_render_graph.need_signal_queue_batch.contains(batch_id) && parsed_render_graph.need_signal_queue_batch[batch_id].contains(queue_type)) {
          auto next_signal_val = used_signal_val[queue_type] + 1;
          command_queue.RegisterSignal(queue_type, next_signal_val);
          used_signal_val.at(queue_type) = next_signal_val;
          batch_signaled_val[batch_id][queue_type] = next_signal_val;
        }
      }
    }
    {
      auto queue_type = parsed_render_graph.frame_end_signal_queue;
      queue_signal_val[i % buffer_num] = {queue_type, used_signal_val.at(queue_type)};
    }
    CHECK(swapchain.Present());
  }
  while (!allocators.empty()) {
    for (auto a : allocators.back()) {
      command_allocator.ReturnCommandAllocator(a);
    }
    allocators.pop_back();
  }
  command_queue.WaitAll();
  command_list.Term();
  command_allocator.Term();
  swapchain.Term();
  window.Term();
  command_queue.Term();
  device.Term();
  dxgi_core.Term();
}
