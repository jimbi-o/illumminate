#include "d3d12_shader_visible_descriptor_heap.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool ShaderVisibleDescriptorHeap::Init(D3d12Device* const device) {
  device_ = device;
  descriptor_heap_buffers_ = CreateDescriptorHeap(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxBufferNum, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  if (descriptor_heap_buffers_ == nullptr) return false;
  heap_start_cpu_buffers_ = descriptor_heap_buffers_->GetCPUDescriptorHandleForHeapStart().ptr;
  heap_start_gpu_buffers_ = descriptor_heap_buffers_->GetGPUDescriptorHandleForHeapStart().ptr;
  buffer_handle_increment_size_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  descriptor_heap_samplers_ = CreateDescriptorHeap(device_, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kMaxSamplerNum, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  if (descriptor_heap_samplers_ == nullptr) return false;
  heap_start_cpu_samplers_ = descriptor_heap_samplers_->GetCPUDescriptorHandleForHeapStart().ptr;
  heap_start_gpu_samplers_ = descriptor_heap_samplers_->GetGPUDescriptorHandleForHeapStart().ptr;
  sampler_handle_increment_size_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  return true;
}
void ShaderVisibleDescriptorHeap::Term() {
  descriptor_heap_buffers_->Release();
  descriptor_heap_samplers_->Release();
}
namespace {
D3D12_GPU_DESCRIPTOR_HANDLE CopyToImpl(D3d12Device* const device, const D3D12_CPU_DESCRIPTOR_HANDLE src, const uint32_t copy_num, uint64_t heap_start_cpu, const D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t * const used_num, const uint32_t max_num, const uint32_t handle_increment_size, const uint64_t heap_start_gpu) {
  if (*used_num + copy_num > max_num) {
    *used_num = copy_num;
    device->CopyDescriptorsSimple(copy_num, D3D12_CPU_DESCRIPTOR_HANDLE{heap_start_cpu}, src, type);
    return {heap_start_gpu};
  }
  auto addr_offset = handle_increment_size * *used_num;
  *used_num += copy_num;
  device->CopyDescriptorsSimple(copy_num, D3D12_CPU_DESCRIPTOR_HANDLE{heap_start_cpu + addr_offset}, src, type);
  return {heap_start_gpu + addr_offset};
}
}
D3D12_GPU_DESCRIPTOR_HANDLE ShaderVisibleDescriptorHeap::CopyToBufferDescriptorHeap(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num) {
  return CopyToImpl(device_, start, num, heap_start_cpu_buffers_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &used_buffer_num_, kMaxBufferNum, buffer_handle_increment_size_, heap_start_gpu_buffers_);
}
D3D12_GPU_DESCRIPTOR_HANDLE ShaderVisibleDescriptorHeap::CopyToSamplerDescriptorHeap(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num) {
  return CopyToImpl(device_, start, num, heap_start_cpu_samplers_, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, &used_sampler_num_, kMaxSamplerNum, sampler_handle_increment_size_, heap_start_gpu_samplers_);
}
void ShaderVisibleDescriptorHeap::SetDescriptorHeapsToCommandList(D3d12CommandList* command_list) {
  ID3D12DescriptorHeap* heaps[2] = {descriptor_heap_buffers_, descriptor_heap_samplers_};
  command_list->SetDescriptorHeaps(2, heaps);
}
}
