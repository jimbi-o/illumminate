#include "d3d12_shader_visible_descriptor_heap.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool ShaderVisibleDescriptorHeap::Init(D3d12Device* const device) {
  device_ = device;
  descriptor_heap_buffers_ = CreateDescriptorHeap(device_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kMaxBufferNum, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  if (descriptor_heap_buffers_ == nullptr) return false;
  heap_start_buffers_ = descriptor_heap_buffers_->GetCPUDescriptorHandleForHeapStart().ptr;
  buffer_handle_increment_size_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  descriptor_heap_samplers_ = CreateDescriptorHeap(device_, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kMaxSamplerNum, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  if (descriptor_heap_samplers_ == nullptr) return false;
  heap_start_samplers_ = descriptor_heap_samplers_->GetCPUDescriptorHandleForHeapStart().ptr;
  sampler_handle_increment_size_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
  return true;
}
void ShaderVisibleDescriptorHeap::Term() {
  descriptor_heap_buffers_->Release();
  descriptor_heap_samplers_->Release();
}
namespace {
void CopyToImpl(D3d12Device* const device, const D3D12_CPU_DESCRIPTOR_HANDLE src, const uint32_t copy_num, uint64_t heap_start, const D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t * const used_num, const uint32_t max_num, const uint32_t handle_increment_size) {
  if (*used_num + copy_num <= max_num) {
    D3D12_CPU_DESCRIPTOR_HANDLE dst = {heap_start + handle_increment_size * *used_num};
    device->CopyDescriptorsSimple(copy_num, dst, src, type);
    *used_num += copy_num;
    return;
  }
  D3D12_CPU_DESCRIPTOR_HANDLE dst[2] = {{heap_start + handle_increment_size * *used_num}, {heap_start}};
  auto space_left = *used_num + copy_num - max_num;
  *used_num = copy_num - space_left;
  uint32_t dst_num_array[2] = {space_left, *used_num};
  device->CopyDescriptors(2, dst, dst_num_array, 1, &src, &copy_num, type);
}
}
void ShaderVisibleDescriptorHeap::CopyToBufferDescriptorHeap(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num) {
  CopyToImpl(device_, start, num, heap_start_buffers_, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &used_buffer_num_, kMaxBufferNum, buffer_handle_increment_size_);
}
void ShaderVisibleDescriptorHeap::CopyToSamplerDescriptorHeap(const D3D12_CPU_DESCRIPTOR_HANDLE start, const uint32_t num) {
  CopyToImpl(device_, start, num, heap_start_samplers_, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, &used_sampler_num_, kMaxSamplerNum, sampler_handle_increment_size_);
}
void ShaderVisibleDescriptorHeap::SetDescriptorHeapsToCommandList(D3d12CommandList* command_list) {
  ID3D12DescriptorHeap* heaps[2] = {descriptor_heap_buffers_, descriptor_heap_samplers_};
  command_list->SetDescriptorHeaps(2, heaps);
}
}
