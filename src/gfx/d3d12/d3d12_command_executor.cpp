#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx {
// TODO move to upper layer directory.
template <typename RenderFunction>
struct RenderGraphPass {
  CommandQueueType command_queue_type;
  RenderFunction render_function;
  std::vector<StrId> srv;
  std::vector<StrId> rtv;
  StrId dsv;
  std::vector<StrId> uav;
  std::vector<StrId> uav_to_clear;
  std::vector<StrId> copy_src;
  std::vector<StrId> copy_dst;
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
  std::unordered_map<BatchId, std::vector<PassId>> pass_order;
  std::unordered_map<PassId, CommandQueueType> pass_command_queue_type;
  std::unordered_map<PassId, std::vector<BarrierInfo>> pre_pass_barriers;
  std::unordered_map<PassId, uint32_t> pass_command_list_index;
  std::unordered_map<PassId, RenderFunction> pass_render_function;
  std::unordered_map<BatchId, std::unordered_map<CommandQueueType, std::vector<BarrierInfo>>> post_batch_barriers;
  std::unordered_map<BatchId, std::unordered_map<CommandQueueType, uint32_t>> post_batch_barriers_command_list_index;
  CommandQueueType frame_end_signal_queue;
};
template <typename RenderFunction>
auto ConfigureQueueWaitInfo(const RenderGraphConfig<RenderFunction>& config) {
  std::unordered_map<BatchId/*consumer*/, std::vector<std::tuple<CommandQueueType/*producer*/, BatchId/*producer*/, CommandQueueType/*consumer*/>>> wait_queue_info;
  // TODO
  return wait_queue_info;
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
TEST_CASE("wait queue info") {
  RenderGraphConfig<void*> config;
  auto info = ConfigureQueueWaitInfo(config);
  CHECK(info.empty());
  config = {
    {{},{},{},{},{},},
  };
  info = ConfigureQueueWaitInfo(config);
  CHECK(info.empty());
  config = {
    // batch 0
    {
    },
  };
  // TODO
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
      for (auto pass_id : parsed_render_graph.pass_order.at(batch_id)) {
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
