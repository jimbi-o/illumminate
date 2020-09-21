#ifndef ILLUMINATE_D3D12_DXGI_CORE_H
#define ILLUMINATE_D3D12_DXGI_CORE_H
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class DxgiCore {
 public:
  bool Init();
  void Term();
  DxgiFactory* GetFactory() { return factory_; }
  DxgiAdapter* GetAdapter() { return adapter_; }
  bool IsTearingAllowed();
 private:
  HMODULE library_ = nullptr;
  DxgiFactory* factory_ = nullptr;
  DxgiAdapter* adapter_ = nullptr;
};
}
#endif
