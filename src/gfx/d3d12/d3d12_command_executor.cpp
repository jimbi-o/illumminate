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
    default:
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
auto GetPassInputResourceList(const RenderGraphPass<RenderFunction>& pass) {
  std::vector<StrId> input_resource_names;
  for (auto& [type, vec] : pass.resources) {
    if (!IsResourceReadable(type)) continue;
    input_resource_names.reserve(input_resource_names.size() + vec.size());
    input_resource_names.insert(input_resource_names.end(), vec.begin(), vec.end());
  }
  return input_resource_names;
}
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
auto GetResourceUsingPass(const std::vector<BatchId>& batch_order, const std::unordered_map<BatchId, std::vector<PassId>>& batched_pass_order, const std::unordered_map<PassId, RenderGraphPass<RenderFunction>>& pass_list, const PassId buffer_used_pass, const StrId buffer_name) {
  std::vector<PassId> used_pass_ids;
  // for (const auto& pass : batch_order | std::views::reverse) { // std::views not ready yet in VC.
  for (auto batch_it = batch_order.crbegin(); batch_it != batch_order.crend(); batch_it++) {
    auto& pass_list = batched_pass_order.at(*batch_it);
    for (auto pass_it = pass_list.crbegin(); pass_it != pass_list.crend(); pass_it++) {
      // TODO
    }
  }
  return used_pass_ids;
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
auto PreparePhysicalResource(const ParsedRenderGraph<RenderFunction>& render_graph) {
  PhysicalResources physical_resources;
  // TODO
  return physical_resources;
}
auto ExecuteResourceBarriers(D3d12CommandList* command_list, const std::vector<BarrierInfo>& barriers, const std::vector<ID3D12Resource*>& resources) {
  // TODO
}
}
namespace {
using namespace illuminate::gfx;
using namespace illuminate::gfx::d3d12;
auto GetRenderGraphSimple() {
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
          {CommandQueueType::kGraphics, 1},
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
          {CommandQueueType::kGraphics, 1},
          {CommandQueueType::kCompute,  2},
        }},
        {{
          {CommandQueueType::kCompute,  4},
          {CommandQueueType::kGraphics, 3},
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
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyD")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyE")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyF")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyG")) != input_list.end());
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
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyA")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyB")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyF")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyG")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyJ")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyK")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyL")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyM")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyN")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyQ")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyR")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyS")) != input_list.end());
    CHECK(std::find(input_list.begin(), input_list.end(), StrId("dmyT")) != input_list.end());
  }
  {
    RenderGraphConfig<uint32_t> config;
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, 0, StrId("swapchain"));
    CHECK(used_pass_id_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("swapchain"));
    CHECK(used_pass_id_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1},
          {CommandQueueType::kCompute,  2},
        }},
        {{
          {CommandQueueType::kCompute,  4},
          {CommandQueueType::kGraphics, 3},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("swapchain"));
    CHECK(used_pass_id_list.empty());
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    SUBCASE("buffer exists") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
      CHECK(used_pass_id_list.size() == 1);
      CHECK(used_pass_id_list[0] == 0);
    }
    SUBCASE("buffer does not exist") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("swapchain"));
      CHECK(used_pass_id_list.empty());
    }
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
    CHECK(used_pass_id_list.size() == 2);
    CHECK(used_pass_id_list[0] == 0);
    CHECK(used_pass_id_list[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("mainbuffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    SUBCASE("search from back") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
      CHECK(used_pass_id_list.size() == 2);
      CHECK(used_pass_id_list[0] == 1);
      CHECK(used_pass_id_list[1] == 2);
    }
    SUBCASE("search from in middle") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, 0, StrId("mainbuffer"));
      CHECK(used_pass_id_list.size() == 1);
      CHECK(used_pass_id_list[0] == 0);
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
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
    CHECK(used_pass_id_list.size() == 3);
    CHECK(used_pass_id_list[0] == 0);
    CHECK(used_pass_id_list[1] == 1);
    CHECK(used_pass_id_list[2] == 2);
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
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("mainbuffer"));
      CHECK(used_pass_id_list.size() == 2);
      CHECK(used_pass_id_list[0] == 2);
      CHECK(used_pass_id_list[1] == 3);
    }
    SUBCASE("search from in middle") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, 3, StrId("mainbuffer"));
      CHECK(used_pass_id_list.size() == 1);
      CHECK(used_pass_id_list[0] == 2);
    }
    SUBCASE("search from in middle 2") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, 2, StrId("mainbuffer"));
      CHECK(used_pass_id_list.size() == 2);
      CHECK(used_pass_id_list[0] == 0);
      CHECK(used_pass_id_list[1] == 1);
    }
    SUBCASE("search from in middle 3") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, 1, StrId("mainbuffer"));
      CHECK(used_pass_id_list.size() == 1);
      CHECK(used_pass_id_list[0] == 0);
    }
    SUBCASE("search from head") {
      auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, 0, StrId("mainbuffer"));
      CHECK(used_pass_id_list.empty());
    }
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvNewBuffer, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadWrite, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadOnly, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("depth"));
    CHECK(used_pass_id_list.size() == 2);
    CHECK(used_pass_id_list[0] == 0);
    CHECK(used_pass_id_list[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvNewBuffer, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadWrite, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kDsvReadOnly, {StrId("depth")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("depth"));
    CHECK(used_pass_id_list.size() == 2);
    CHECK(used_pass_id_list[0] == 1);
    CHECK(used_pass_id_list[1] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavNewBuffer, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadOnly, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(used_pass_id_list.size() == 2);
    CHECK(used_pass_id_list[0] == 1);
    CHECK(used_pass_id_list[1] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvNewBuffer, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kCopyDst, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kUavReadOnly, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(used_pass_id_list.size() == 2);
    CHECK(used_pass_id_list[0] == 1);
    CHECK(used_pass_id_list[1] == 2);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(used_pass_id_list.size() == 1);
    CHECK(used_pass_id_list[0] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(used_pass_id_list.size() == 1);
    CHECK(used_pass_id_list[0] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
        }},
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(used_pass_id_list.size() == 2);
    CHECK(used_pass_id_list[0] == 0);
    CHECK(used_pass_id_list[1] == 1);
  }
  {
    RenderGraphConfig<uint32_t> config = {{
        {{
          {CommandQueueType::kCompute, 1, {{ResourceStateType::kUavReadWrite, {StrId("buffer")}}}},
        }},
        {{
          {CommandQueueType::kGraphics, 1, {{ResourceStateType::kRtvPrevResultReused, {StrId("buffer")}}}},
          {CommandQueueType::kGraphics, 1},
        }}
      }
    };
    auto [batch_order, batched_pass_order, pass_list] = ConfigureBatchedPassList(std::move(config));
    auto used_pass_id_list = GetResourceUsingPass(batch_order, batched_pass_order, pass_list, batched_pass_order.at(batch_order.back()).back(), StrId("buffer"));
    CHECK(used_pass_id_list.size() == 2);
    CHECK(used_pass_id_list[0] == 0);
    CHECK(used_pass_id_list[1] == 1);
  }
}
TEST_CASE("validate render graph config") {
  // TODO CorrectBatchedConsumerProducerNotInSameQueue()
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
        command_queue.Get(queue_type)->ExecuteCommandLists(command_list_num, (ID3D12CommandList**)command_lists.at(queue_type));
        command_list.ReturnCommandList(command_lists[queue_type]);
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
