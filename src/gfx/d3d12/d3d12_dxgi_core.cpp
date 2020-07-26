#include "d3d12_dxgi_core.h"
#include "minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool DxgiCore::Init() {
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  library_ = LoadLibrary("Dxgi.dll");
  ASSERT(library_ && "failed to load Dxgi.dll");
  decltype(&CreateDXGIFactory2) CreateDXGIFactory2 = reinterpret_cast<decltype(CreateDXGIFactory2)>(GetProcAddress(library_, "CreateDXGIFactory2"));
  ASSERT(CreateDXGIFactory2 && "load CreateDXGIFactory2 failed");
  auto hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory_));
  ASSERT(SUCCEEDED(hr) && factory_ && "CreateDXGIFactory2 failed", hr);
  hr = factory_->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter_));
  ASSERT(SUCCEEDED(hr) && adapter_ && "EnumAdapterByGpuPreference failed", hr);
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
#include "doctest/doctest.h"
TEST_CASE("dxgi core") {
  using namespace illuminate::gfx::d3d12;
  DxgiCore core;
  CHECK(core.Init());
  core.Term();
}
