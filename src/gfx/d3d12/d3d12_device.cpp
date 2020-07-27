#include "d3d12_device.h"
#include "Windows.h"
#ifndef SHIP_BUILD
#include "d3d12sdklayers.h"
#endif
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool Device::Init(DxgiAdapter* const adapter) {
  library_ = LoadLibrary("D3D12.dll");
  ASSERT(library_ && "LoadLibrary(D3D12.dll) failed");
#ifndef SHIP_BUILD
  if (IsDebuggerPresent()) {
    ID3D12Debug* debug_interface = nullptr;
    if (SUCCEEDED(CALL(D3D12GetDebugInterface)(IID_PPV_ARGS(&debug_interface)))) {
      debug_interface->EnableDebugLayer();
      loginfo("EnableDebugLayer");
      ID3D12Debug1* debug_interface1 = nullptr;
      if (SUCCEEDED(debug_interface->QueryInterface(IID_PPV_ARGS(&debug_interface1)))) {
        debug_interface1->SetEnableGPUBasedValidation(true);
        loginfo("SetEnableGPUBasedValidation");
        debug_interface1->Release();
      }
      debug_interface->Release();
    }
  }
#endif
  auto hr = CALL(D3D12CreateDevice)(adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device_));
  ASSERT(SUCCEEDED(hr) && device_ && "D3D12CreateDevice failed.", hr);
  return true;
#ifndef SHIP_BUILD
  if (IsDebuggerPresent()) {
    ID3D12InfoQueue* info_queue = nullptr;
    if (SUCCEEDED(device_->QueryInterface(IID_PPV_ARGS(&info_queue)))) {
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
      info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
      D3D12_MESSAGE_SEVERITY suppressed_severity[] = {
        D3D12_MESSAGE_SEVERITY_INFO,
      };
      // D3D12_MESSAGE_ID supressed_id[] = {
      // };
      D3D12_INFO_QUEUE_FILTER queue_filter = {};
      queue_filter.DenyList.NumSeverities = _countof(suppressed_severity);
      queue_filter.DenyList.pSeverityList = suppressed_severity;
      // queue_filter.DenyList.NumIDs = _countof(supressed_id);
      // queue_filter.DenyList.pIDList = supressed_id;
      info_queue->PushStorageFilter(&queue_filter);
      info_queue->Release();
    }
  }
#endif
};
void Device::Term() {
  device_->Release();
  FreeLibrary(library_);
};
}
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
TEST_CASE("device") {
  using namespace illuminate::gfx::d3d12;
  DxgiCore core;
  CHECK(core.Init());
  Device device;
  CHECK(device.Init(core.GetAdapter()));
  device.Term();
  core.Term();
}
