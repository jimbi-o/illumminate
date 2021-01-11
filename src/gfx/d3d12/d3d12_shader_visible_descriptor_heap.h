#ifndef ILLUMINATE_D3D12_SHADER_VISIBLE_DESCRIPTOR_HEAP_H
#define ILLUMINATE_D3D12_SHADER_VISIBLE_DESCRIPTOR_HEAP_H
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class ShaderVisibleDescriptorHeap {
 public:
  bool Init(D3d12Device* const device);
  void Term();
  void CopyToBuffer(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num);
  void CopyToSampler(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num);
  void SetDescriptorHeapsToCommandList(D3d12CommandList* command_list);
 private:
  static const uint32_t kMaxBufferNum = 1024;
  static const uint32_t kMaxSamplerNum = 32;
  D3d12Device* device_ = nullptr;
  ID3D12DescriptorHeap* descriptor_heap_buffers_ = nullptr;
  ID3D12DescriptorHeap* descriptor_heap_samplers_ = nullptr;
  uint32_t used_buffer_num_ = 0;
  uint32_t used_sampler_num_ = 0;
  uint32_t buffer_handle_increment_size_ = 0;
  uint32_t sampler_handle_increment_size_ = 0;
};
}
#endif
