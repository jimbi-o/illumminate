#ifndef ILLUMINATE_D3D12_UTIL_H
#define ILLUMINATE_D3D12_UTIL_H
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
ID3D12DescriptorHeap* CreateDescriptorHeap(D3d12Device* const device,
                                           const D3D12_DESCRIPTOR_HEAP_TYPE type,
                                           const uint32_t descriptor_num,
                                           const D3D12_DESCRIPTOR_HEAP_FLAGS flags);
}
#endif
