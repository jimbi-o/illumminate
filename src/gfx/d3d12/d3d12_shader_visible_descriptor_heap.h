#ifndef ILLUMINATE_D3D12_SHADER_VISIBLE_DESCRIPTOR_HEAP_H
#define ILLUMINATE_D3D12_SHADER_VISIBLE_DESCRIPTOR_HEAP_H
#include "d3d12_header_common.h"
#ifdef BUILD_WITH_TEST
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
#endif
namespace illuminate::gfx::d3d12 {
class ShaderVisibleDescriptorHeap {
 public:
  bool Init(D3d12Device* const device);
  void Term();
  D3D12_GPU_DESCRIPTOR_HANDLE CopyToBufferDescriptorHeap(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num);
  D3D12_GPU_DESCRIPTOR_HANDLE CopyToSamplerDescriptorHeap(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num);
  void SetDescriptorHeapsToCommandList(D3d12CommandList* command_list);
 private:
  static const uint32_t kMaxBufferNum = 1024;
  static const uint32_t kMaxSamplerNum = 32;
  D3d12Device* device_ = nullptr;
  ID3D12DescriptorHeap* descriptor_heap_buffers_ = nullptr;
  ID3D12DescriptorHeap* descriptor_heap_samplers_ = nullptr;
  uint64_t heap_start_cpu_buffers_ = 0;
  uint64_t heap_start_cpu_samplers_ = 0;
  uint64_t heap_start_gpu_buffers_ = 0;
  uint64_t heap_start_gpu_samplers_ = 0;
  uint32_t used_buffer_num_ = 0;
  uint32_t used_sampler_num_ = 0;
  uint32_t buffer_handle_increment_size_ = 0;
  uint32_t sampler_handle_increment_size_ = 0;
#include "d3d12_shader_visible_descriptor_heap_test.inl"
};
}
#endif
