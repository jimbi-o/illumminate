#include "d3d12_descriptor_heap.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
DescriptorHeap::~DescriptorHeap() {
  Term();
}
bool DescriptorHeap::Init(D3d12Device* const device, const D3D12_DESCRIPTOR_HEAP_TYPE heap_type, const uint32_t descriptor_handle_num) {
  CreateDescriptorHeap(device, heap_type, descriptor_handle_num, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, &descriptor_heap_);
  if (descriptor_heap_ == nullptr) return false;
  heap_start_cpu_ = descriptor_heap_->GetCPUDescriptorHandleForHeapStart().ptr;
  handle_increment_size_ = device->GetDescriptorHandleIncrementSize(heap_type);
  return true;
}
void DescriptorHeap::Term() {
  if (descriptor_heap_) {
    descriptor_heap_->Release();
    descriptor_heap_ = nullptr;
  }
}
}
