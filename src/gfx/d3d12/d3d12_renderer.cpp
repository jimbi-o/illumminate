#include <dxgi1_6.h>
#include "renderer.h"
#include "logger.h"
namespace illuminate::gfx::d3d12 {
class RendererImpl : public illuminate::gfx::RendererInterface {
 public:
  void ExecuteBatchedRendererPass(const BatchedRendererPass* const batch_list, const uint32_t batch_num, const BufferDescList& global_buffer_descs) {
    loginfo("Hello world!");
  }
};
using DxgiFactory = IDXGIFactory7;
using DxgiAdapter = IDXGIAdapter4;
class DxgiCore {
 public:
  bool Init();
  void Term();
  DxgiFactory* GetFactory() { return factory_; }
  DxgiAdapter* GetAdapter() { return adapter_; }
 private:
  HMODULE library_ = nullptr;
  DxgiFactory* factory_ = nullptr;
  DxgiAdapter* adapter_ = nullptr;
};
bool DxgiCore::Init() {
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  uint32_t flag = 0;
#ifndef BUILD_TYPE_SHIP
  flag |= DXGI_CREATE_FACTORY_DEBUG;
#endif
  library_ = LoadLibrary("Dxgi.dll");
  if (library_ == nullptr) return false;
  decltype(&CreateDXGIFactory2) CreateDXGIFactory2 = reinterpret_cast<decltype(CreateDXGIFactory2)>(GetProcAddress(library_, "CreateDXGIFactory2"));
  auto hr = CreateDXGIFactory2(flag, IID_PPV_ARGS(&factory_));
  if (FAILED(hr)) {
    logfatal("CreateDXGIFactory2 failed. {}", hr);
    return false;
  }
  if (factory_ == nullptr) return false;
  hr = factory_->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_));
  if (FAILED(hr)) {
    logfatal("EnumAdapterByGpuPreference failed. {}", hr);
    return false;
  }
  if (adapter_ == nullptr) return false;
  {
    DXGI_ADAPTER_DESC1 desc;
    adapter_->GetDesc1(&desc);
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      logwarn("IDXGIAdapter is software");
    }
  }
  return true;
};
void DxgiCore::Term() {
  factory_->Release();
  adapter_->Release();
  FreeLibrary(library_);
};
}
namespace illuminate::gfx {
RendererInterface* CreateRendererD3d12() {
  return new illuminate::gfx::d3d12::RendererImpl();
}
}
#include "doctest/doctest.h"
TEST_CASE("") {
  using namespace illuminate::gfx::d3d12;
  DxgiCore core;
  CHECK(core.Init());
  core.Term();
}
