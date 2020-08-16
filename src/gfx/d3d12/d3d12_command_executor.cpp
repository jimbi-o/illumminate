#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
struct RenderPassPhysicalResourceConfig {
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv;
};
using RenderPassFunction = std::function<void(D3d12CommandList** command_list, const RenderPassPhysicalResourceConfig& resource)>;
struct RenderPassResourceConfig {
  std::vector<StrId> rtv;
};
struct RenderPassConfig {
  RenderPassFunction render_function;
  RenderPassResourceConfig resource_config;
};
using RenderPassList = std::vector<RenderPassConfig>;
struct BatchedRenderPassConfig {
  RenderPassList pass;
};
struct PhysicalResourceInfo {
  ID3D12Resource* resource;
  D3D12_CPU_DESCRIPTOR_HANDLE handle;
  D3D12_RESOURCE_STATES initial_state;
  D3D12_RESOURCE_STATES final_state;
};
using PhysicalResourceInfoList = std::unordered_map<StrId, PhysicalResourceInfo>;
struct RenderGraphConfig {
  std::vector<BatchedRenderPassConfig> batch;
  PhysicalResourceInfoList physical_resource_info;
};
using BarrierList = std::vector<D3D12_RESOURCE_BARRIER>;
struct ParsedRenderGraphPassInfo {
  std::unordered_map<CommandQueueType, uint32_t> command_list_index_prev_function;
  std::unordered_map<CommandQueueType, uint32_t> command_list_index_render_function;
  std::unordered_map<CommandQueueType, uint32_t> command_list_index_post_function;
  RenderPassFunction render_function;
  RenderPassPhysicalResourceConfig resource_config;
  BarrierList barriers_pre_pass;
  BarrierList barriers_post_pass;
};
struct ParsedRenderGraphBatchInfo {
  std::unordered_map<CommandQueueType, uint32_t> command_list_num;
  std::vector<ParsedRenderGraphPassInfo> pass;
};
struct ParsedRenderGraphInfo {
  std::vector<ParsedRenderGraphBatchInfo> batch;
};
ParsedRenderGraphInfo ParseRenderGraph(RenderGraphConfig&& graph_config) {
  ParsedRenderGraphInfo parsed_graph{};
  for (auto& src_batch : graph_config.batch) {
    ParsedRenderGraphBatchInfo dst_batch{};
    dst_batch.command_list_num[CommandQueueType::kGraphics] = 1; // TODO consider queue types used, parallel exec
    for (auto& src_pass : src_batch.pass) {
      ParsedRenderGraphPassInfo dst_pass{};
      dst_pass.command_list_index_prev_function[CommandQueueType::kGraphics] = 0; // TODO
      dst_pass.command_list_index_render_function[CommandQueueType::kGraphics] = 0; // TODO
      dst_pass.command_list_index_post_function[CommandQueueType::kGraphics] = 0; // TODO
      dst_pass.render_function = std::move(src_pass.render_function);
      dst_pass.resource_config.rtv.reserve(src_pass.resource_config.rtv.size());
      std::unordered_map<StrId, std::vector<PhysicalResourceInfo>> used_resources;
      used_resources[SID("rtv")].reserve(src_pass.resource_config.rtv.size());
      for (auto buffer_name : src_pass.resource_config.rtv) {
        auto& physical_resource_info = graph_config.physical_resource_info[buffer_name];
        dst_pass.resource_config.rtv.push_back(physical_resource_info.handle);
        used_resources[SID("rtv")].push_back(physical_resource_info);
      }
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      for (auto& physical_resource : used_resources[SID("rtv")]) {
        barrier.Transition.pResource   = physical_resource.resource;
        barrier.Transition.StateBefore = physical_resource.initial_state;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
        dst_pass.barriers_pre_pass.push_back(barrier);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = physical_resource.final_state;
        dst_pass.barriers_post_pass.push_back(barrier);
      }
      dst_batch.pass.push_back(std::move(dst_pass));
    }
    parsed_graph.batch.push_back(std::move(dst_batch));
  }
  return parsed_graph;
}
void ExecuteResourceBarriers(D3d12CommandList* const command_list, const BarrierList& barriers) {
  command_list->ResourceBarrier(barriers.size(), barriers.data());
}
}
#include "doctest/doctest.h"
#include <queue>
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
  for (uint32_t i = 0; i < 2 * buffer_num; i++) {
    CAPTURE(i);
    if (i >= buffer_num) {
      command_queue.WaitOnCpu({{CommandQueueType::kGraphics, i - buffer_num}});
      for (auto a : allocators.front()) {
        command_allocator.ReturnCommandAllocator(a);
      }
      allocators.pop();
    }
    allocators.push({});
    swapchain.UpdateBackBufferIndex();
    RenderGraphConfig config {
      { // batch
        { // batch[0]
          { // pass[0]
            { // render_function
              [](D3d12CommandList** command_list, const RenderPassPhysicalResourceConfig& resource) {
                const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
                command_list[0]->ClearRenderTargetView(resource.rtv[0], clear_color, 0, nullptr);
              },
              { // resource_config
                /*rtv:*/ {{SID("mainbuffer")}}
              }
            }
          }
        }
      },
      { // physical_resource_info
        {SID("mainbuffer"), {swapchain.GetResource(), swapchain.GetRtvHandle(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_PRESENT}}
      }
    };
    auto parsed_render_graph = ParseRenderGraph(std::move(config));
    for (auto& batch : parsed_render_graph.batch) {
      auto command_allocators = command_allocator.RetainCommandAllocator(CommandQueueType::kGraphics, batch.command_list_num[CommandQueueType::kGraphics]);
      auto command_lists = command_list.RetainCommandList(CommandQueueType::kGraphics, batch.command_list_num[CommandQueueType::kGraphics], command_allocators);
      allocators.back().push_back(std::move(command_allocators));
      for (auto& pass : batch.pass) {
        ExecuteResourceBarriers(command_lists[pass.command_list_index_prev_function[CommandQueueType::kGraphics]], pass.barriers_pre_pass);
        pass.render_function(&command_lists[pass.command_list_index_render_function[CommandQueueType::kGraphics]], pass.resource_config);
        ExecuteResourceBarriers(command_lists[pass.command_list_index_post_function[CommandQueueType::kGraphics]], pass.barriers_post_pass);
      }
      for (uint32_t j = 0; j < batch.command_list_num[CommandQueueType::kGraphics]; j++) {
        auto hr = command_lists[j]->Close();
        if (FAILED(hr)) {
          logwarn("close command list failed. {}", hr);
          continue;
        }
      }
      command_queue.GetCommandQueue(CommandQueueType::kGraphics)->ExecuteCommandLists(1, (ID3D12CommandList**)command_lists);
      command_queue.RegisterSignal(CommandQueueType::kGraphics, i + 1);
      command_list.ReturnCommandList(command_lists);
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
