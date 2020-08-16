#include <queue>
#include "d3d12_minimal_for_cpp.h"
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "gfx/win32/win32_window.h"
#include "d3d12_swapchain.h"
namespace illuminate::gfx::d3d12 {
struct RenderPassResourceConfig {
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv;
};
using RenderPassFunction = std::function<void(D3d12CommandList** command_list, const RenderPassResourceConfig& resource)>;
struct RenderPassConfig {
  RenderPassFunction render_function;
  RenderPassResourceConfig resource_config;
};
using RenderPassList = std::vector<RenderPassConfig>;
struct BatchedRenderPassConfig {
  RenderPassList pass;
};
struct RenderGraphConfig {
  std::vector<BatchedRenderPassConfig> batch;
};
struct ParsedRenderGraphPassInfo {
  std::unordered_map<CommandListType, uint32_t> command_list_index_prev_function;
  std::unordered_map<CommandListType, uint32_t> command_list_index_render_function;
  std::unordered_map<CommandListType, uint32_t> command_list_index_post_function;
  RenderPassFunction render_function;
  RenderPassResourceConfig resource_config;
};
struct ParsedRenderGraphBatchInfo {
  std::unordered_map<CommandListType, uint32_t> command_list_num;
  std::vector<ParsedRenderGraphPassInfo> pass;
};
struct ParsedRenderGraphInfo {
  std::vector<ParsedRenderGraphBatchInfo> batch;
};
ParsedRenderGraphInfo ParseRenderGraph(RenderGraphConfig&& graph_config) {
  ParsedRenderGraphInfo parsed_graph{};
  for (auto& src_batch : graph_config.batch) {
    ParsedRenderGraphBatchInfo dst_batch{};
    dst_batch.command_list_num[CommandListType::kGraphics] = 1; // TODO consider queue types used, parallel exec
    for (auto& src_pass : src_batch.pass) {
      ParsedRenderGraphPassInfo dst_pass{};
      dst_pass.command_list_index_prev_function[CommandListType::kGraphics] = 0; // TODO
      dst_pass.command_list_index_render_function[CommandListType::kGraphics] = 0; // TODO
      dst_pass.command_list_index_post_function[CommandListType::kGraphics] = 0; // TODO
      dst_pass.render_function = std::move(src_pass.render_function);
      dst_pass.resource_config.rtv = std::move(src_pass.resource_config.rtv);
      dst_batch.pass.push_back(std::move(dst_pass));
    }
    parsed_graph.batch.push_back(std::move(dst_batch));
  }
  return parsed_graph;
}
}
TEST_CASE("execute command list") {
  const uint32_t buffer_num = 2;
  const uint32_t swapchain_buffer_num = buffer_num + 1;
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
  CHECK(swapchain.Init(dxgi_core.GetFactory(), command_queue.GetCommandQueue(CommandListType::kGraphics), device.GetDevice(), window.GetHwnd(), swapchain_buffer_num, buffer_num));
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.GetDevice()));
  CommandList command_list;
  CHECK(command_list.Init(device.GetDevice()));
  std::queue<std::vector<ID3D12CommandAllocator**>> allocators;
  for (uint32_t i = 0; i < 2 * buffer_num; i++) {
    CAPTURE(i);
    if (i >= buffer_num) {
      command_queue.WaitOnCpu({{CommandListType::kGraphics, i - buffer_num}});
      for (auto a : allocators.back()) {
        command_allocator.ReturnCommandAllocator(a);
      }
      allocators.pop();
    }
    allocators.push({});
    swapchain.UpdateBackBufferIndex();
    BatchedRenderPassConfig config {
      {
        {
          [](D3d12CommandList** command_list, const RenderPassResourceConfig& resource) {
            const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
            command_list[0]->ClearRenderTargetView(resource.rtv[0], clear_color, 0, nullptr);
          },
          {
            {swapchain.GetRtvHandle()}
          }
        }
      }
    };
    auto parsed_render_graph = ParseRenderGraph({{std::move(config)}});
    {
      auto& batch = parsed_render_graph.batch[0];
      auto command_allocators = command_allocator.RetainCommandAllocator(CommandListType::kGraphics, batch.command_list_num[CommandListType::kGraphics]);
      auto command_lists = command_list.RetainCommandList(CommandListType::kGraphics, batch.command_list_num[CommandListType::kGraphics], command_allocators);
      allocators.back().push_back(std::move(command_allocators));
      {
        auto& pass = batch.pass[0];
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource   = swapchain.GetResource();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        command_lists[pass.command_list_index_prev_function[CommandListType::kGraphics]]->ResourceBarrier(1, &barrier);
        pass.render_function(&command_lists[pass.command_list_index_render_function[CommandListType::kGraphics]], pass.resource_config);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
        command_lists[pass.command_list_index_post_function[CommandListType::kGraphics]]->ResourceBarrier(1, &barrier);
      }
      for (uint32_t j = 0; j < batch.command_list_num[CommandListType::kGraphics]; j++) {
        auto hr = command_lists[j]->Close();
        if (FAILED(hr)) {
          logwarn("close command list failed. {}", hr);
          continue;
        }
      }
      command_queue.GetCommandQueue(CommandListType::kGraphics)->ExecuteCommandLists(1, (ID3D12CommandList**)command_lists);
      command_queue.RegisterSignal(CommandListType::kGraphics, i + 1);
      command_list.ReturnCommandList(command_lists);
    }
    CHECK(swapchain.Present());
  }
  while (!allocators.empty()) {
    for (auto a : allocators.back()) {
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
