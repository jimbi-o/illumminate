#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
void CreateDescriptorHeap(D3d12Device* const device,
                          const D3D12_DESCRIPTOR_HEAP_TYPE type,
                          const uint32_t descriptor_num,
                          const D3D12_DESCRIPTOR_HEAP_FLAGS flags,
                          ID3D12DescriptorHeap** dst) {
  D3D12_DESCRIPTOR_HEAP_DESC descriptor_heap_desc = { type, descriptor_num, flags, 0 /*node mask*/ };
  auto hr = device->CreateDescriptorHeap(&descriptor_heap_desc, IID_PPV_ARGS(dst));
  if (FAILED(hr)) {
    logerror("CreateDescriptorHeap failed. {} {} {} {}", hr, type, descriptor_num, flags);
  }
}
}
