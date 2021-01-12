#ifndef ILLUMINATE_D3D12_DESCRIPTOR_HEAP_H
#define ILLUMINATE_D3D12_DESCRIPTOR_HEAP_H
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class DescriptorHeap {
 public:
  ~DescriptorHeap();
  bool Init(D3d12Device* const device, const D3D12_DESCRIPTOR_HEAP_TYPE heap_type, const uint32_t descriptor_handle_num);
  void Term();
  constexpr D3D12_CPU_DESCRIPTOR_HANDLE RetainHandle() {
    auto addr = heap_start_cpu_ + handle_increment_size_ * used_handle_num_;
    used_handle_num_++;
    return {addr};
  }
 private:
  ID3D12DescriptorHeap* descriptor_heap_ = nullptr;
  uint64_t heap_start_cpu_ = 0;
  uint32_t used_handle_num_ = 0;
  uint32_t handle_increment_size_ = 0;
};
}
#endif
