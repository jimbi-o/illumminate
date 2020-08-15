#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
ID3D12DescriptorHeap* CreateDescriptorHeap(D3d12Device* const device,
                                           const D3D12_DESCRIPTOR_HEAP_TYPE type,
                                           const uint32_t descriptor_num,
                                           const D3D12_DESCRIPTOR_HEAP_FLAGS flags) {
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = { type, descriptor_num, flags, 0 /*node mask*/ };
  ID3D12DescriptorHeap* descriptor_heap = nullptr;
  auto hr = device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(&descriptor_heap));
  if (FAILED(hr)) {
    logerror("CreateDescriptorHeap failed. {} {} {} {}", hr, type, descriptor_num, flags);
    return nullptr;
  }
  return descriptor_heap;
}
}
