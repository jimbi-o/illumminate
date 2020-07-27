#ifndef __ILLUMINATE_D3D12_DXGI_CORE_H__
#define __ILLUMINATE_D3D12_DXGI_CORE_H__
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
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
}
#endif
