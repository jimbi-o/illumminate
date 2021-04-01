TEST_CASE_CLASS("descriptor heap") {
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  ShaderVisibleDescriptorHeap descriptor_heap;
  CHECK(descriptor_heap.Init(device.Get()));
  CHECK(descriptor_heap.descriptor_heap_buffers_);
  CHECK(descriptor_heap.descriptor_heap_samplers_);
  CHECK(descriptor_heap.heap_start_cpu_buffers_ == descriptor_heap.descriptor_heap_buffers_->GetCPUDescriptorHandleForHeapStart().ptr);
  CHECK(descriptor_heap.heap_start_cpu_samplers_ == descriptor_heap.descriptor_heap_samplers_->GetCPUDescriptorHandleForHeapStart().ptr);
  CHECK(descriptor_heap.heap_start_gpu_buffers_ == descriptor_heap.descriptor_heap_buffers_->GetGPUDescriptorHandleForHeapStart().ptr);
  CHECK(descriptor_heap.heap_start_gpu_samplers_ == descriptor_heap.descriptor_heap_samplers_->GetGPUDescriptorHandleForHeapStart().ptr);
  {
    auto desc = descriptor_heap.descriptor_heap_buffers_->GetDesc();
    CHECK(desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CHECK(desc.NumDescriptors == kMaxBufferNum);
    CHECK(desc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  }
  {
    auto desc = descriptor_heap.descriptor_heap_samplers_->GetDesc();
    CHECK(desc.Type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    CHECK(desc.NumDescriptors == kMaxSamplerNum);
    CHECK(desc.Flags == D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE);
  }
  CHECK(descriptor_heap.used_buffer_num_ == 0);
  CHECK(descriptor_heap.used_sampler_num_ == 0);
  CHECK(descriptor_heap.buffer_handle_increment_size_ == device.Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
  CHECK(descriptor_heap.sampler_handle_increment_size_ == device.Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER));
  descriptor_heap.Term();
}
