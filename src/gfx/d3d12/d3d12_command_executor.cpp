#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx {
// TODO move to upper layer directory.
template <typename RenderFunction>
struct RenderGraphConfig {
};
struct BarrierInfo {
};
template <typename RenderFunction>
struct ParsedRenderGraphPass {
  CommandQueueType command_queue_type;
  uint32_t command_list_index_main;
  uint32_t command_list_index_post;
  RenderFunction render_function;
  std::vector<BarrierInfo> barriers_pre_pass;
  std::vector<BarrierInfo> barriers_post_pass;
};
template <typename RenderFunction>
struct ParsedRenderGraphBatch {
  StrId batch_name;
  std::vector<std::tuple<CommandQueueType, StrId, CommandQueueType>> wait_queues;
  std::unordered_map<CommandQueueType, uint32_t> command_list_num;
  std::vector<ParsedRenderGraphPass<RenderFunction>> pass;
};
template <typename RenderFunction>
struct ParsedRenderGraph {
  std::vector<ParsedRenderGraphBatch<RenderFunction>> batch;
  CommandQueueType frame_signal_queue;
};
template <typename RenderFunction>
ParsedRenderGraph<RenderFunction> ParseRenderGraph(RenderGraphConfig<RenderFunction>&& render_graph_config) {
  // TODO
  return {};
}
}
#include <queue>
namespace illuminate::gfx::d3d12 {
struct PhysicalResources {
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv;
};
using RenderFunction = std::function<void(D3d12CommandList**, const PhysicalResources&)>;
using PhysicalResourceList = std::queue<std::queue<PhysicalResources>>;
PhysicalResourceList PreparePhysicalResource(const ParsedRenderGraph<RenderFunction>& render_graph) {
  // TODO
  return {};
}
void ExecuteResourceBarriers(D3d12CommandList* command_list, const std::vector<BarrierInfo>& barriers, const PhysicalResources& physical_resource) {
  // TODO
}
}
namespace {
using namespace illuminate::gfx;
using namespace illuminate::gfx::d3d12;
RenderFunction GetRenderFunctionClearRtv() {
  return [](D3d12CommandList** command_list, const PhysicalResources& resources) {
    const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
    command_list[0]->ClearRenderTargetView(resources.rtv[0], clear_color, 0, nullptr);
  };
}
// TODO add d3d12 command functions: clear dsv, uav, set rtv|dsv, root sigs (graphics|compute)
RenderGraphConfig<RenderFunction> GetRenderGraphSimple() {
  return {};
}
using CreateRenderGraphFunc = std::function<RenderGraphConfig<RenderFunction>()>;
}
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "gfx/win32/win32_window.h"
#include "d3d12_swapchain.h"
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
  CHECK(command_queue.Init(device.GetDevice()));
  illuminate::gfx::win32::Window window;
  CHECK(window.Init("swapchain test", 160, 90));
  Swapchain swapchain;
  CHECK(swapchain.Init(dxgi_core.GetFactory(), command_queue.GetCommandQueue(CommandQueueType::kGraphics), device.GetDevice(), window.GetHwnd(), swapchain_buffer_num, buffer_num));
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.GetDevice()));
  CommandList command_list;
  CHECK(command_list.Init(device.GetDevice()));
  std::queue<std::vector<ID3D12CommandAllocator**>> allocators;
  std::vector<std::tuple<CommandQueueType, uint64_t>> queue_signal_val(buffer_num);
  std::unordered_map<CommandQueueType, uint64_t> used_signal_val;
  CreateRenderGraphFunc create_render_graph_func;
  SUBCASE("simple graph") {
    create_render_graph_func = GetRenderGraphSimple;
  }
  for (uint32_t i = 0; i < 10 * buffer_num; i++) {
    CAPTURE(i);
    {
      const auto [queue_type, val] = queue_signal_val[i % buffer_num];
      if (val > 0) {
        command_queue.WaitOnCpu({{queue_type, val}});
        for (auto a : allocators.front()) {
          command_allocator.ReturnCommandAllocator(a);
        }
        allocators.pop();
      }
    }
    allocators.push({});
    swapchain.UpdateBackBufferIndex();
    auto parsed_render_graph = ParseRenderGraph(create_render_graph_func());
    auto physical_resouce = PreparePhysicalResource(parsed_render_graph);
    std::unordered_map<StrId, std::unordered_map<CommandQueueType, uint64_t>> batch_signaled_val;
    for (auto& batch : parsed_render_graph.batch) {
      std::unordered_map<CommandQueueType, D3d12CommandList**> command_lists;
      for (auto& pair : batch.command_list_num) {
        auto queue_type = pair.first;
        auto command_allocators = command_allocator.RetainCommandAllocator(queue_type, batch.command_list_num[queue_type]);
        command_lists[queue_type] = command_list.RetainCommandList(queue_type, batch.command_list_num[queue_type], command_allocators);
        allocators.back().push_back(std::move(command_allocators));
      }
      for (auto [wait_queue, signaled_batch, signaled_queue] : batch.wait_queues) {
        command_queue.RegisterWaitOnQueue(signaled_queue, batch_signaled_val[signaled_batch][signaled_queue], wait_queue);
      }
      auto&& batch_physical_resource = std::move(physical_resouce.front());
      physical_resouce.pop();
      for (auto& pass : batch.pass) {
        auto queue_type = pass.command_queue_type;
        auto&& pass_physical_resource = std::move(batch_physical_resource.front());
        batch_physical_resource.pop();
        ExecuteResourceBarriers(command_lists[queue_type][pass.command_list_index_main], pass.barriers_pre_pass, pass_physical_resource);
        pass.render_function(&command_lists[queue_type][pass.command_list_index_main], pass_physical_resource);
        ExecuteResourceBarriers(command_lists[queue_type][pass.command_list_index_post], pass.barriers_post_pass, pass_physical_resource);
      }
      for (auto& pair : batch.command_list_num) {
        auto queue_type = pair.first;
        for (uint32_t j = 0; j < pair.second; j++) {
          auto hr = command_lists[queue_type][j]->Close();
          if (FAILED(hr)) {
            logwarn("close command list failed. {}", hr);
            continue;
          }
        }
        command_queue.GetCommandQueue(queue_type)->ExecuteCommandLists(1, (ID3D12CommandList**)command_lists[queue_type]);
        auto next_signal_val = used_signal_val[queue_type] + 1;
        used_signal_val[queue_type] = next_signal_val;
        command_queue.RegisterSignal(queue_type, next_signal_val);
        batch_signaled_val[batch.batch_name][queue_type] = next_signal_val;
        command_list.ReturnCommandList(command_lists[queue_type]);
      }
    }
    {
      auto queue_type = parsed_render_graph.frame_signal_queue;
      queue_signal_val[i % buffer_num] = {queue_type, used_signal_val[queue_type]};
    }
    CHECK(swapchain.Present());
  }
  while (!allocators.empty()) {
    for (auto a : allocators.front()) {
      command_allocator.ReturnCommandAllocator(a);
    }
    allocators.pop();
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
