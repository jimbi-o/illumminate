#ifndef __ILLUMINATE_D3D12_DEVICE_H__
#define __ILLUMINATE_D3D12_DEVICE_H__
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class Device {
 public:
  bool Init(DxgiAdapter* const);
  void Term();
 private:
  HMODULE library_ = nullptr;
  D3d12Device* device_ = nullptr;
};
}
#endif
