#ifndef __ILLUMINATE_D3D12_SWAPCHAIN_H__
#define __ILLUMINATE_D3D12_SWAPCHAIN_H__
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class Swapchain {
 public:
  // https://developer.nvidia.com/dx12-dos-and-donts
  // Try using about 1-2 more swap-chain buffers than you are intending to queue frames (in terms of command allocators and dynamic data and the associated frame fences) and set the "max frame latency" to this number of swap-chain buffers.
  bool Init(DxgiFactory* factory, ID3D12CommandQueue* command_queue_graphics, D3d12Device* const device, HWND hwnd, const uint32_t swapchain_buffer_num, const uint32_t frame_latency);
  void Term();
  void EnableVsync(const bool b) { vsync_ = b; }
  void UpdateBackBufferIndex();
  bool Present();
  constexpr ID3D12Resource* GetResource() { return resources_[buffer_index_]; }
  constexpr D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle() const { return cpu_handles_rtv_[buffer_index_]; }
 private:
  DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
  uint32_t swapchain_buffer_num_ = 0;
  bool vsync_ = true;
  bool tearing_support_ = false;
  uint32_t width_ = 0, height_ = 0;
  DxgiSwapchain* swapchain_ = nullptr;
  HANDLE frame_latency_waitable_object_ = nullptr;
  std::vector<ID3D12Resource*> resources_;
  ID3D12DescriptorHeap* descriptor_heap_ = nullptr;
  std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> cpu_handles_rtv_;
  uint32_t buffer_index_ = 0;
};
}
#endif
