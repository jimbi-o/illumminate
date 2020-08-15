#include "d3d12_minimal_for_cpp.h"
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
  std::vector<ID3D12CommandAllocator**> allocators;
  for (uint32_t i = 0; i < 2 * buffer_num; i++) {
    CAPTURE(i);
    if (i >= buffer_num) {
      command_queue.WaitOnCpu({{CommandListType::kGraphics, i - buffer_num}});
      auto it = allocators.begin();
      command_allocator.ReturnCommandAllocator(*it);
      allocators.erase(it);
    }
    auto command_allocators = command_allocator.RetainCommandAllocator(CommandListType::kGraphics, 1);
    allocators.push_back(command_allocators);
    auto command_lists = command_list.RetainCommandList(CommandListType::kGraphics, 1, command_allocators);
    swapchain.UpdateBackBufferIndex();
    // TODO barrier common->rtv->common
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource   = swapchain.GetResource();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    command_lists[0]->ResourceBarrier(1, &barrier);
    auto handle = swapchain.GetRtvHandle();
    FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
    command_lists[0]->ClearRenderTargetView(handle, clear_color, 0, nullptr);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
    command_lists[0]->ResourceBarrier(1, &barrier);
    auto hr = command_lists[0]->Close();
    if (FAILED(hr)) {
      logwarn("close command list failed. {}", hr);
      continue;
    }
    command_queue.GetCommandQueue(CommandListType::kGraphics)->ExecuteCommandLists(1, (ID3D12CommandList**)command_lists);
    command_queue.RegisterSignal(CommandListType::kGraphics, i + 1);
    command_list.ReturnCommandList(command_lists);
    CHECK(swapchain.Present());
  }
  while (!allocators.empty()) {
    auto it = allocators.begin();
    command_allocator.ReturnCommandAllocator(*it);
    allocators.erase(it);
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
