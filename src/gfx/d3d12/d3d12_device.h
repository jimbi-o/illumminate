#ifndef ILLUMINATE_D3D12_DEVICE_H
#define ILLUMINATE_D3D12_DEVICE_H
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class Device {
 public:
  bool Init(DxgiAdapter* const);
  void Term();
  constexpr D3d12Device* Get() { return device_; }
 private:
  HMODULE library_ = nullptr;
  D3d12Device* device_ = nullptr;
};
}
#endif
