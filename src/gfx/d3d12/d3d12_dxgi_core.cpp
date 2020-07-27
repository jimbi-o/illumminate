#include "d3d12_dxgi_core.h"
#include "d3d12_minimal_for_cpp.h"
#ifndef SHIP_BUILD
#include "DXGIDebug.h"
#endif
namespace illuminate::gfx::d3d12 {
bool DxgiCore::Init() {
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  library_ = LoadLibrary("Dxgi.dll");
  ASSERT(library_ && "failed to load Dxgi.dll");
  LoadDllFunction(library_, CreateDXGIFactory2);  
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
  adapter_->Release();
  factory_->Release();
#ifndef SHIP_BUILD
  IDXGIDebug1* debug = nullptr;
  LoadDllFunction(library_, DXGIGetDebugInterface1);
  auto hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug));
  if (FAILED(hr)) {
    logwarn("DXGIGetDebugInterface failed. {}", hr);
    return;
  }
  debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
  debug->Release();
#endif
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
