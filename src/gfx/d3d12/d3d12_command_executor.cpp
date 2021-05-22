#include "d3d12_minimal_for_cpp.h"
#include "render_graph.h"
#include <optional>
#include <ranges>
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
  (void) render_graph;
  PhysicalResources physical_resources;
  // TODO
  return physical_resources;
}
static auto ExecuteResourceBarriers(D3d12CommandList* command_list, const std::vector<BarrierInfo>& barriers, const std::vector<ID3D12Resource*>& resources) {
  (void) command_list;
  (void) barriers;
  (void) resources;
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
#include "illuminate/gfx/win32/win32_window.h"
#include "d3d12_swapchain.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
TEST_CASE("execute command list") { // NOLINT
  const uint32_t buffer_num = 2;
  const uint32_t swapchain_buffer_num = buffer_num + 1;
  using namespace illuminate::gfx; // NOLINT
  using namespace illuminate::gfx::d3d12; // NOLINT
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init()); // NOLINT
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter())); // NOLINT
  CommandQueue command_queue;
  CHECK(command_queue.Init(device.Get())); // NOLINT
  illuminate::gfx::win32::Window window;
  CHECK(window.Init("swapchain test", 160, 90)); // NOLINT
  Swapchain swapchain;
  CHECK(swapchain.Init(dxgi_core.GetFactory(), command_queue.Get(CommandQueueType::kGraphics), device.Get(), window.GetHwnd(), swapchain_buffer_num, buffer_num)); // NOLINT
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.Get())); // NOLINT
  CommandList command_list;
  CHECK(command_list.Init(device.Get())); // NOLINT
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
    CHECK(swapchain.Present()); // NOLINT
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
#ifdef __clang__
#pragma clang diagnostic pop
#endif
