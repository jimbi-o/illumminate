#include "d3d12_swapchain.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool Swapchain::Init(DxgiFactory* factory, ID3D12CommandQueue* command_queue_graphics, D3d12Device* const device, HWND hwnd, const uint32_t swapchain_buffer_num, const uint32_t frame_latency) {
  format_ = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapchain_buffer_num_ = swapchain_buffer_num;
  // tearing support
  {
    BOOL result = false; // doesn't work with "bool".
    auto hr = factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &result, sizeof(result));
    if (FAILED(hr)) {
      logwarn("CheckFeatureSupport(tearing) failed. {}", hr);
    }
    logtrace("Tearing support:{}", result);
    tearing_support_ = result;
  }
  // disable alt+enter fullscreen
  {
    auto hr = factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
    if (FAILED(hr)) {
      logwarn("MakeWindowAssociation failed. {}", hr);
    }
  }
  // create swapchain
  {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = desc.Height = 0; // get value from hwnd
    desc.Format = format_;
    desc.Stereo = false;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = swapchain_buffer_num_;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = (tearing_support_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0) | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    IDXGISwapChain1* swapchain = nullptr;
    auto hr = factory->CreateSwapChainForHwnd(command_queue_graphics, hwnd, &desc, nullptr, nullptr, &swapchain);
    ASSERT(SUCCEEDED(hr) && "CreateSwapChainForHwnd failed. {}");
    hr = swapchain->QueryInterface(IID_PPV_ARGS(&swapchain_));
    ASSERT(SUCCEEDED(hr) && "swapchain->QueryInterface failed. {}");
    swapchain->Release();
  }
  // set max frame latency
  {
    auto hr = swapchain_->SetMaximumFrameLatency(frame_latency);
    if (FAILED(hr)) {
      logwarn("SetMaximumFrameLatency failed. {} {}", hr, frame_latency);
    }
  }
  // get frame latency object
  {
    frame_latency_waitable_object_ = swapchain_->GetFrameLatencyWaitableObject();
  }
  // get swapchain params
  {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    auto hr = swapchain_->GetDesc1(&desc);
    if (FAILED(hr)) {
      logwarn("swapchain_->GetDesc1 failed. {}", hr);
    } else {
      width_ = desc.Width;
      height_ = desc.Height;
      format_ = desc.Format;
      swapchain_buffer_num_ = desc.BufferCount;
    }
  }
  // get swapchain resource buffers for rtv
  {
    resources_.reserve(swapchain_buffer_num_);
    for (uint32_t i = 0; i < swapchain_buffer_num_; i++) {
      ID3D12Resource* resource = nullptr;
      auto hr = swapchain_->GetBuffer(i, IID_PPV_ARGS(&resource));
      if (FAILED(hr)) {
        logwarn("swapchain_->GetBuffer failed. {} {}", i, hr);
        return false;
      }
      SET_NAME(resource, "swapchain", i);
      resources_.push_back(std::move(resource));
    }
  }
  // prepare rtv
  {
    descriptor_heap_ = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, swapchain_buffer_num_, D3D12_DESCRIPTOR_HEAP_FLAG_NONE);
    ASSERT(descriptor_heap_ && "CreateDescriptorHeap for swapchain rtv failed.");
    descriptor_heap_->SetName(L"descriptorheap_rtv");
    const D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {format_, D3D12_RTV_DIMENSION_TEXTURE2D, {}};
    auto rtv_handle = descriptor_heap_->GetCPUDescriptorHandleForHeapStart();
    auto rtv_step_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    for (uint32_t i = 0; i < swapchain_buffer_num_; i++) {
      device->CreateRenderTargetView(resources_[i], &rtv_desc, rtv_handle);
      cpu_handles_rtv_.push_back(rtv_handle);
      rtv_handle.ptr += rtv_step_size;
    }
  }
  return true;
}
void Swapchain::Term() {
  if (descriptor_heap_) descriptor_heap_->Release();
  for (auto&& resource : resources_) {
    resource->Release();
  }
  if (swapchain_) swapchain_->Release();
}
void Swapchain::UpdateBackBufferIndex() {
  auto hr = WaitForSingleObjectEx(frame_latency_waitable_object_, INFINITE, FALSE);
  if (FAILED(hr)) {
    logwarn("WaitForSingleObjectEx failed. {}", hr);
    return;
  }
  buffer_index_ = swapchain_->GetCurrentBackBufferIndex();
}
bool Swapchain::Present() {
  auto hr = swapchain_->Present(vsync_ ? 1 : 0, (!vsync_ && tearing_support_) ? DXGI_PRESENT_ALLOW_TEARING : 0);
  if (FAILED(hr)) {
    logwarn("Swapchain::Present failed. {}", hr);
    return false;
  }
  return true;
}
}
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
#include "d3d12_command_queue.h"
#include "gfx/win32/win32_window.h"
TEST_CASE("swapchain") {
  const uint32_t swapchain_buffer_num = 3;
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
  CHECK(swapchain.Init(dxgi_core.GetFactory(), command_queue.Get(CommandQueueType::kGraphics), device.Get(), window.GetHwnd(), swapchain_buffer_num, swapchain_buffer_num - 1));
  for (uint32_t i = 0; i < swapchain_buffer_num + 1; i++) {
    swapchain.UpdateBackBufferIndex();
    CHECK(swapchain.Present());
  }
  command_queue.WaitAll();
  swapchain.Term();
  window.Term();
  command_queue.Term();
  device.Term();
  dxgi_core.Term();
}
