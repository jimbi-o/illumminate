#include "D3D12MemAlloc.h"
#include "d3dx12.h"
#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "d3d12_descriptor_heap.h"
#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_minimal_for_cpp.h"
#include "d3d12_shader_compiler.h"
#include "d3d12_shader_visible_descriptor_heap.h"
#include "d3d12_swapchain.h"
#include "illuminate/gfx/win32/win32_window.h"
#include "render_graph.h"
namespace illuminate::gfx::d3d12 {
class DeviceSet {
 public:
  bool Init(const uint32_t frame_buffer_num, const illuminate::gfx::BufferSize2d& swapchain_size, const uint32_t swapchain_buffer_num) {
    if (!dxgi_core.Init()) return false;
    if (!device.Init(dxgi_core.GetAdapter())) return false;
    if (!command_queue.Init(device.Get())) return false;
    if (!window.Init("swapchain test", swapchain_size.width, swapchain_size.height)) return false;
    if (!swapchain.Init(dxgi_core.GetFactory(), command_queue.Get(CommandQueueType::kGraphics), device.Get(), window.GetHwnd(), swapchain_buffer_num, frame_buffer_num)) return false;
    return true;
  }
  void Term() {
    swapchain.Term();
    window.Term();
    command_queue.Term();
    device.Term();
    dxgi_core.Term();
  }
  constexpr auto GetDevice() { return device.Get(); }
  auto GetCommandQueue(const CommandQueueType type) { return command_queue.Get(type); }
  void WaitAll() { command_queue.WaitAll(); }
  DxgiCore dxgi_core;
  Device device;
  CommandQueue command_queue;
  illuminate::gfx::win32::Window window;
  Swapchain swapchain;
};
class CommandListSet {
 public:
  CommandListSet(std::pmr::memory_resource* memory_resource)
      : allocator_buffer_(memory_resource)
      , command_list_in_use_(memory_resource)
      , command_list_num_in_use_(memory_resource)
  {
  }
  bool Init(D3d12Device* device, const uint32_t allocator_buffer_len) {
    if (!allocator_.Init(device)) return false;
    if (!pool_.Init(device)) return false;
    allocator_buffer_index_ = 0;
    allocator_buffer_len_ = allocator_buffer_len;
    allocator_buffer_.resize(allocator_buffer_len_);
    return true;
  }
  void Term() {
    for (uint32_t i = 0; i < allocator_buffer_len_; i++) {
      ReturnCommandAllocatorInBuffer(i);
    }
    pool_.Term();
    allocator_.Term();
  }
  void RotateCommandAllocators() {
    allocator_buffer_index_ = (allocator_buffer_index_ + 1) % allocator_buffer_len_;
    ReturnCommandAllocatorInBuffer(allocator_buffer_index_);
    ASSERT(command_list_in_use_.empty());
    ASSERT(command_list_num_in_use_.empty());
  }
  D3d12CommandList** GetCommandList(const CommandQueueType command_queue_type, const uint32_t num) {
    if (command_list_in_use_.contains(command_queue_type)) {
      ASSERT(command_list_num_in_use_.at(command_queue_type) == num);
      return command_list_in_use_.at(command_queue_type);
    }
    auto allocators = allocator_.RetainCommandAllocator(command_queue_type, num);
    auto command_list = pool_.RetainCommandList(command_queue_type, num, allocators);
    allocator_buffer_[allocator_buffer_index_].push_back(allocators);
    command_list_in_use_.emplace(command_queue_type, command_list);
    command_list_num_in_use_.emplace(command_queue_type, num);
    return command_list;
  }
  void ExecuteCommandLists(ID3D12CommandQueue* command_queue, const CommandQueueType command_queue_type) {
    auto& list = command_list_in_use_.at(command_queue_type);
    auto& list_len = command_list_num_in_use_.at(command_queue_type);
    for (uint32_t i = 0; i < list_len; i++) {
      list[i]->Close();
    }
    command_queue->ExecuteCommandLists(list_len, reinterpret_cast<ID3D12CommandList**>(list));
    pool_.ReturnCommandList(list);
    command_list_in_use_.erase(command_queue_type);
    command_list_num_in_use_.erase(command_queue_type);
  }
 private:
  void ReturnCommandAllocatorInBuffer(const uint32_t buffer_index) {
    for (auto& a : allocator_buffer_[buffer_index]) {
      allocator_.ReturnCommandAllocator(a);
    }
    allocator_buffer_[buffer_index].clear();
  }
  CommandAllocator allocator_;
  CommandList pool_;
  uint32_t allocator_buffer_index_ = 0;
  uint32_t allocator_buffer_len_ = 0;
  vector<vector<ID3D12CommandAllocator**>> allocator_buffer_;
  unordered_map<CommandQueueType, D3d12CommandList**> command_list_in_use_;
  unordered_map<CommandQueueType, uint32_t> command_list_num_in_use_;
};
class ShaderResourceSet {
 public:
  using DepthStencilEnableFlag = illuminate::core::EnableDisable;
  ShaderResourceSet(std::pmr::memory_resource* memory_resource)
      : rootsig_list_(memory_resource)
      , pso_list_(memory_resource)
  {}
  bool Init(D3d12Device* device) {
    if (!shader_compiler_.Init(device)) return false;
    return true;
  }
  void Term() {
    for (auto& [key, rootsig] : rootsig_list_) {
      rootsig->Release();
    }
    for (auto& [key, pso] : pso_list_) {
      pso->Release();
    }
    shader_compiler_.Term();
  }
  std::tuple<ID3D12RootSignature*, ID3D12PipelineState*> CreateVsPsPipelineStateObject(const StrId& pso_id, D3d12Device* const device, const StrId& rootsig_id, LPCWSTR vs, LPCWSTR ps, D3D12_RT_FORMAT_ARRAY&& output_dxgi_format, DepthStencilEnableFlag enable_depth_stencil, std::pmr::memory_resource* memory_resource_work) {
    if (rootsig_list_.contains(rootsig_id) && pso_list_.contains(pso_id)) {
      return {rootsig_list_.at(rootsig_id), pso_list_.at(pso_id)};
    }
    auto shader_result_ps = shader_compiler_.Compile(ps, ShaderType::kPs, memory_resource_work);
    if (!rootsig_list_.contains(rootsig_id)) {
      auto root_signature_blob = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_ps, DXC_OUT_ROOT_SIGNATURE);
      ID3D12RootSignature* root_signature = nullptr;
      auto hr = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
      root_signature_blob->Release();
      if (FAILED(hr)) {
        shader_result_ps->Release();
        return {};
      }
      root_signature->SetName(L"rootsig-vsps");
      rootsig_list_.emplace(rootsig_id, root_signature);
    }
    auto root_signature = rootsig_list_.at(rootsig_id);
    auto shader_result_vs = shader_compiler_.Compile(vs, ShaderType::kVs, memory_resource_work);
    auto shader_object_vs = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_vs, DXC_OUT_OBJECT);
    auto shader_object_ps = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_ps, DXC_OUT_OBJECT);
    struct {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature;
      CD3DX12_PIPELINE_STATE_STREAM_VS vs;
      CD3DX12_PIPELINE_STATE_STREAM_PS ps;
      CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS render_target_formats;
      CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY topology;
      CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depth_stencil;
    } desc_local {
      root_signature,
      D3D12_SHADER_BYTECODE{shader_object_vs->GetBufferPointer(), shader_object_vs->GetBufferSize()},
      D3D12_SHADER_BYTECODE{shader_object_ps->GetBufferPointer(), shader_object_ps->GetBufferSize()},
      std::move(output_dxgi_format),
      D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    };
    if (enable_depth_stencil == DepthStencilEnableFlag::kDisabled) {
      ((CD3DX12_DEPTH_STENCIL_DESC&)(desc_local.depth_stencil)).DepthEnable = false;
    }
    D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(desc_local), &desc_local};
    ID3D12PipelineState* pipeline_state = nullptr;
    auto hr = device->CreatePipelineState(&desc, IID_PPV_ARGS(&pipeline_state));
    shader_object_ps->Release();
    shader_object_vs->Release();
    shader_result_ps->Release();
    shader_result_vs->Release();
    if (FAILED(hr)) return {};
    pipeline_state->SetName(L"pso-vsps");
    pso_list_.emplace(pso_id, pipeline_state);
    return {root_signature, pipeline_state};
  }
  std::tuple<ID3D12RootSignature*, ID3D12PipelineState*> CreateCsPipelineStateObject(const StrId& pso_id, D3d12Device* const device, const StrId& rootsig_id, LPCWSTR cs, std::pmr::memory_resource* memory_resource_work) {
    if (rootsig_list_.contains(rootsig_id) && pso_list_.contains(pso_id)) {
      return {rootsig_list_.at(rootsig_id), pso_list_.at(pso_id)};
    }
    auto shader_result_cs = shader_compiler_.Compile(cs, ShaderType::kCs, memory_resource_work);
    if (!rootsig_list_.contains(rootsig_id)) {
      auto root_signature_blob = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_cs, DXC_OUT_ROOT_SIGNATURE);
      ID3D12RootSignature* root_signature = nullptr;
      auto hr = device->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
      root_signature_blob->Release();
      if (FAILED(hr)) {
        shader_result_cs->Release();
        return {};
      }
      root_signature->SetName(L"rootsig-cs");
      rootsig_list_.emplace(rootsig_id, root_signature);
    }
    auto root_signature = rootsig_list_.at(rootsig_id);
    auto shader_object_cs = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_cs, DXC_OUT_OBJECT);
    struct {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature;
      CD3DX12_PIPELINE_STATE_STREAM_CS cs;
    } desc_local {
      root_signature,
      D3D12_SHADER_BYTECODE{shader_object_cs->GetBufferPointer(), shader_object_cs->GetBufferSize()},
    };
    D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(desc_local), &desc_local};
    ID3D12PipelineState* pipeline_state = nullptr;
    auto hr = device->CreatePipelineState(&desc, IID_PPV_ARGS(&pipeline_state));
    shader_object_cs->Release();
    shader_result_cs->Release();
    if (FAILED(hr)) return {};
    pipeline_state->SetName(L"pso-cs");
    pso_list_.emplace(pso_id, pipeline_state);
    return {root_signature, pipeline_state};
  }
  ID3D12RootSignature* GetRootSignature(const StrId& rootsig_id) {
    return rootsig_list_.at(rootsig_id);
  }
  ID3D12PipelineState* GetPipelineStateObject(const StrId& pso_id) {
    return pso_list_.at(pso_id);
  }
 private:
  ShaderCompiler shader_compiler_;
  unordered_map<StrId, ID3D12RootSignature*> rootsig_list_;
  unordered_map<StrId, ID3D12PipelineState*> pso_list_;
};
using PhysicalBufferId = uint32_t;
class PhysicalBufferSet {
 public:
  PhysicalBufferSet(std::pmr::memory_resource* memory_resource)
      : allocation_list_(memory_resource)
      , resource_list_(memory_resource)
  {
  }
  bool Init(D3d12Device* device, DxgiAdapter* const adapter) {
    D3D12MA::ALLOCATOR_DESC desc = {};
    desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
    desc.pDevice = device;
    desc.pAdapter = adapter;
    auto hr = D3D12MA::CreateAllocator(&desc, &allocator_);
    if (FAILED(hr)) return false;
    buffer_id_used_ = 0;
    return true;
  }
  void Term() {
    for (auto& [buffer_id, allocation] : allocation_list_) {
      resource_list_.at(buffer_id)->Release();
      allocation->Release();
    }
    allocator_->Release();
  }
  PhysicalBufferId CreatePhysicalBuffer(const D3D12_HEAP_TYPE heap_type, const D3D12_RESOURCE_STATES initial_state, const D3D12_RESOURCE_DESC& resource_desc, const D3D12_CLEAR_VALUE* clear_value) {
    D3D12MA::ALLOCATION_DESC allocation_desc{};
    allocation_desc.HeapType = heap_type;
    D3D12MA::Allocation* allocation = nullptr;
    ID3D12Resource* resource = nullptr;
    auto hr = allocator_->CreateResource(&allocation_desc, &resource_desc, initial_state, clear_value, &allocation, IID_PPV_ARGS(&resource));
    if (FAILED(hr)) {
      logerror("CreatePhysicalBuffer failed. {} {} {}", buffer_id_used_, hr, heap_type);
      return {};
    }
    buffer_id_used_++;
    allocation_list_.emplace(buffer_id_used_, allocation);
    resource_list_.emplace(buffer_id_used_, resource);
    return buffer_id_used_;
  }
  ID3D12Resource* GetPhysicalBuffer(const BufferId buffer_id) {
    return resource_list_.at(buffer_id);
  }
 private:
  D3D12MA::Allocator* allocator_ = nullptr;
  unordered_map<BufferId, D3D12MA::Allocation*> allocation_list_;
  unordered_map<BufferId, ID3D12Resource*> resource_list_;
  PhysicalBufferId buffer_id_used_ = 0;
};
class DescriptorHeapSet {
 public:
  DescriptorHeapSet(std::pmr::memory_resource* memory_resource)
      : heaps_(memory_resource)
      , handles_(memory_resource)
  {
    handles_[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV] = unordered_map<BufferId, D3D12_CPU_DESCRIPTOR_HANDLE>(memory_resource);
    handles_[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER] = unordered_map<BufferId, D3D12_CPU_DESCRIPTOR_HANDLE>(memory_resource);
    handles_[D3D12_DESCRIPTOR_HEAP_TYPE_RTV] = unordered_map<BufferId, D3D12_CPU_DESCRIPTOR_HANDLE>(memory_resource);
    handles_[D3D12_DESCRIPTOR_HEAP_TYPE_DSV] = unordered_map<BufferId, D3D12_CPU_DESCRIPTOR_HANDLE>(memory_resource);
    ASSERT(handles_.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
  }
  bool Init(D3d12Device* device) {
    if (!heaps_[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 256)) return false;
    if (!heaps_[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER].Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16)) return false;
    if (!heaps_[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 32)) return false;
    if (!heaps_[D3D12_DESCRIPTOR_HEAP_TYPE_DSV].Init(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 32)) return false;
    ASSERT(heaps_.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
    return true;
  }
  void Term() {
    for (auto& [type, heap] : heaps_) {
      heap.Term();
    }
  }
  auto RetainHandle(const BufferId buffer_id, const D3D12_DESCRIPTOR_HEAP_TYPE type) {
    if (!handles_.at(type).contains(buffer_id)) {
      auto handle = heaps_.at(type).RetainHandle();
      handles_.at(type).emplace(buffer_id, handle);
    }
    return handles_.at(type).at(buffer_id);
  }
  auto GetHandle(const BufferId buffer_id, const D3D12_DESCRIPTOR_HEAP_TYPE type) {
    return handles_.at(type).at(buffer_id);
  }
 private:
  unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, DescriptorHeap> heaps_;
  unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, unordered_map<BufferId, D3D12_CPU_DESCRIPTOR_HANDLE>> handles_;
};
constexpr D3D12_RESOURCE_STATES ConvertToD3d12ResourceState(const BufferStateFlags& flags) {
  D3D12_RESOURCE_STATES state{};
  if (flags & kBufferStateFlagCbvUpload) {
    state |= D3D12_RESOURCE_STATE_GENERIC_READ;
  }
  if (flags & kBufferStateFlagSrvPsOnly) {
    state |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  }
  if (flags & kBufferStateFlagSrvNonPs) {
    state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  }
  if (flags & kBufferStateFlagUav) {
    state |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  }
  if (flags & kBufferStateFlagRtv) {
    state |= D3D12_RESOURCE_STATE_RENDER_TARGET;
  }
  if (flags & kBufferStateFlagDsvWrite) {
    state |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
  }
  if (flags & kBufferStateFlagDsvRead) {
    state |= D3D12_RESOURCE_STATE_DEPTH_READ;
  }
  if (flags & kBufferStateFlagCopySrc) {
    state |= D3D12_RESOURCE_STATE_COPY_SOURCE;
  }
  if (flags & kBufferStateFlagCopyDst) {
    state |= D3D12_RESOURCE_STATE_COPY_DEST;
  }
  if (flags & kBufferStateFlagPresent) {
    state |= D3D12_RESOURCE_STATE_PRESENT;
  }
  return state;
}
constexpr D3D12_RESOURCE_STATES GetInitialD3d12ResourceStateFlag(const BufferConfig& buffer_config) {
  return ConvertToD3d12ResourceState(buffer_config.initial_state_flags);
}
constexpr D3D12_RESOURCE_FLAGS ConvertToD3d12ResourceFlags(const BufferStateFlags state_flags) {
  D3D12_RESOURCE_FLAGS flags{};
  if (state_flags & kBufferStateFlagUav) flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (state_flags & kBufferStateFlagRtv) flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if ((state_flags & kBufferStateFlagDsvWrite) || (state_flags & kBufferStateFlagDsvRead)) flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if ((flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) && !(state_flags & kBufferStateFlagSrvPsOnly) && !(state_flags & kBufferStateFlagSrvNonPs)) flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  return flags;
}
constexpr D3D12_RESOURCE_DIMENSION ConvertToD3d12ResourceDimension(const BufferDimensionType type) {
  switch (type) {
    case BufferDimensionType::kBuffer:    return D3D12_RESOURCE_DIMENSION_BUFFER;
    case BufferDimensionType::k1d:        return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case BufferDimensionType::k1dArray:   return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case BufferDimensionType::k2d:        return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case BufferDimensionType::k2dArray:   return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case BufferDimensionType::k3d:        return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    case BufferDimensionType::kCube:      return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case BufferDimensionType::kCubeArray: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case BufferDimensionType::kAS:        return D3D12_RESOURCE_DIMENSION_BUFFER; // TODO check
  }
  return D3D12_RESOURCE_DIMENSION_BUFFER;
}
constexpr D3D12_RESOURCE_DESC ConvertToD3d12ResourceDesc(const BufferConfig& buffer_config) {
  return {
    .Dimension = ConvertToD3d12ResourceDimension(buffer_config.dimension),
    .Alignment = 0,
    .Width = buffer_config.width,
    .Height = buffer_config.height,
    .DepthOrArraySize = 1,
    .MipLevels = 1,
    .Format = GetDxgiFormat(buffer_config.format),
    .SampleDesc = {1, 0},
    .Layout = (buffer_config.dimension == BufferDimensionType::kBuffer && buffer_config.format == BufferFormat::kUnknown) ? D3D12_TEXTURE_LAYOUT_ROW_MAJOR : D3D12_TEXTURE_LAYOUT_UNKNOWN,
    .Flags = ConvertToD3d12ResourceFlags(buffer_config.state_flags),
  };
}
constexpr bool IsClearValueValid(const BufferConfig& buffer_config) {
  if (buffer_config.state_flags && kBufferStateFlagRtv) return true;
  if (buffer_config.state_flags && kBufferStateFlagDsvWrite) return true;
  return false;
}
constexpr D3D12_CLEAR_VALUE GetD3d12ClearValue(const BufferConfig& buffer_config) {
  if (std::holds_alternative<ClearValueDepthStencil>(buffer_config.clear_value)) {
    auto&& depth_stencil = GetClearValueDepthBuffer(buffer_config.clear_value);
    return {
      .Format = GetDxgiFormat(buffer_config.format),
      .DepthStencil = {
        .Depth = depth_stencil.depth,
        .Stencil = depth_stencil.stencil,
      },
    };
  }
  auto&& color = GetClearValueColorBuffer(buffer_config.clear_value);
  return {
    .Format = GetDxgiFormat(buffer_config.format),
    .Color = {color[0], color[1], color[2], color[3],},
  };
}
constexpr D3D12_RENDER_TARGET_VIEW_DESC ConvertToD3d12RtvDesc(const BufferConfig& buffer_config) {
  return {
    .Format = GetDxgiFormat(buffer_config.format),
    .ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D,
    .Texture2D{
      .MipSlice = 0,
      .PlaneSlice = 0,
    },
  };
}
constexpr BufferConfig GetBufferConfigUploadBuffer(const uint32_t size_in_bytes) {
  return {
    .width = size_in_bytes,
    .height = 1,
    .dimension = BufferDimensionType::kBuffer,
    .format = BufferFormat::kUnknown,
    .state_flags = kBufferStateFlagCbvUpload,
    .initial_state_flags = kBufferStateFlagCbvUpload,
    .clear_value = {},
  };
}
using FrameBufferedBufferId = uint32_t;
class FrameBufferedBufferSet {
 public:
  FrameBufferedBufferSet(std::pmr::memory_resource* memory_resource) : mapped_ptr_(memory_resource), memory_resource_(memory_resource) {
  }
  FrameBufferedBufferId RegisterFrameBufferedBuffers(ID3D12Resource** resource_list, const uint32_t resource_list_len) {
    buffer_id_used_++;
    mapped_ptr_.emplace(buffer_id_used_, vector<void*>{memory_resource_});
    auto& ptr_list = mapped_ptr_.at(buffer_id_used_);
    ptr_list.resize(resource_list_len);
    D3D12_RANGE read_range{};
    for (uint32_t i = 0; i < resource_list_len; i++) {
      auto hr = resource_list[i]->Map(0, &read_range, &ptr_list[i]);
      if (FAILED(hr)) {
        return {};
      }
    }
    return buffer_id_used_;
  }
  void* GetFrameBufferedBuffers(FrameBufferedBufferId buffer_id, const uint32_t index) {
    return mapped_ptr_.at(buffer_id)[index];
  }
 private:
  unordered_map<FrameBufferedBufferId, vector<void*>> mapped_ptr_;
  std::pmr::memory_resource* memory_resource_;
  FrameBufferedBufferId buffer_id_used_ = 0;
};
struct SignalValues {
  unordered_map<CommandQueueType, uint64_t> used_signal_val;
  vector<unordered_map<CommandQueueType, uint64_t>> frame_wait_signal;
  SignalValues(std::pmr::memory_resource* memory_resource, const uint32_t frame_buffer_num)
      : used_signal_val(memory_resource)
      , frame_wait_signal(frame_buffer_num, memory_resource)
  {}
};
void SignalQueueOnFrameEnd(CommandQueue* const command_queue, CommandQueueType command_queue_type, unordered_map<CommandQueueType, uint64_t>* const used_signal_val, unordered_map<CommandQueueType, uint64_t>* const frame_wait_signal) {
  auto& signal_val = ++(*used_signal_val)[command_queue_type];
  command_queue->RegisterSignal(command_queue_type, signal_val);
  (*frame_wait_signal)[command_queue_type] = signal_val;
}
}
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_offset_in_bytes_persistent = 0;
const uint32_t buffer_size_in_bytes_persistent = 16 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_persistent + buffer_size_in_bytes_persistent;
const uint32_t buffer_size_in_bytes_scene = 4 * 1024;
const uint32_t buffer_offset_in_bytes_frame = buffer_offset_in_bytes_scene + buffer_size_in_bytes_scene;
const uint32_t buffer_size_in_bytes_frame = 4 * 1024;
const uint32_t buffer_offset_in_bytes_work = buffer_offset_in_bytes_frame + buffer_size_in_bytes_frame;
const uint32_t buffer_size_in_bytes_work = 4 * 1024;
std::byte buffer[buffer_offset_in_bytes_work + buffer_size_in_bytes_work]{};
const uint32_t kTestFrameNum = 10;
}
#endif
#include "doctest/doctest.h"
TEST_CASE("create pso") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  CHECK(shader_resource_set.Init(devices.GetDevice()));
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  D3D12_RT_FORMAT_ARRAY array{{devices.swapchain.GetDxgiFormat()}, 1};
  auto [rootsig, pso] = shader_resource_set.CreateVsPsPipelineStateObject(StrId("pso_id"), devices.GetDevice(), StrId("rootsig_tmp"), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/copysrv.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  CHECK(rootsig);
  CHECK(pso);
  memory_resource_work.Reset();
  CHECK(shader_resource_set.GetRootSignature(StrId("rootsig_tmp")) == rootsig);
  CHECK(shader_resource_set.GetPipelineStateObject(StrId("pso_id")) == pso);
  std::tie(rootsig, pso) = shader_resource_set.CreateCsPipelineStateObject(StrId("pso_id_cs"), devices.GetDevice(), StrId("rootsig_cs_tmp"), L"shader/test/fill-screen.cs.hlsl", &memory_resource_work);
  CHECK(rootsig);
  CHECK(pso);
  CHECK(shader_resource_set.GetRootSignature(StrId("rootsig_cs_tmp")) == rootsig);
  CHECK(shader_resource_set.GetPipelineStateObject(StrId("pso_id_cs")) == pso);
  shader_resource_set.Term();
  devices.Term();
}
TEST_CASE("clear swapchain buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  CHECK(signal_values.used_signal_val.empty());
  CHECK(signal_values.frame_wait_signal.size() == frame_buffer_num);
  for (auto& map : signal_values.frame_wait_signal) {
    CHECK(map.empty());
  }
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    command_list_set.RotateCommandAllocators();
    devices.swapchain.UpdateBackBufferIndex();
    {
      auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
      D3D12_RESOURCE_BARRIER barrier{};
      {
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = devices.swapchain.GetResource();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      command_list->ResourceBarrier(1, &barrier);
      {
        const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
        command_list->ClearRenderTargetView(devices.swapchain.GetRtvHandle(), clear_color, 0, nullptr);
      }
      {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
      }
      command_list->ResourceBarrier(1, &barrier);
      command_list_set.ExecuteCommandLists(devices.GetCommandQueue(CommandQueueType::kGraphics), CommandQueueType::kGraphics);
    }
    devices.swapchain.Present();
    SignalQueueOnFrameEnd(&devices.command_queue, CommandQueueType::kGraphics, &signal_values.used_signal_val, &signal_values.frame_wait_signal[frame_index]);
  }
  devices.WaitAll();
  command_list_set.Term();
  devices.Term();
}
TEST_CASE("draw to swapchain buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  CHECK(signal_values.used_signal_val.empty());
  CHECK(signal_values.frame_wait_signal.size() == frame_buffer_num);
  for (auto& map : signal_values.frame_wait_signal) {
    CHECK(map.empty());
  }
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  auto [rootsig, pso] = shader_resource_set.CreateVsPsPipelineStateObject(StrId("pso"), devices.GetDevice(), StrId("rootsig_tmp"), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/test.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    command_list_set.RotateCommandAllocators();
    devices.swapchain.UpdateBackBufferIndex();
    {
      auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
      D3D12_RESOURCE_BARRIER barrier{};
      {
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = devices.swapchain.GetResource();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      command_list->ResourceBarrier(1, &barrier);
      {
        auto& width = swapchain_size.width;
        auto& height = swapchain_size.height;
        command_list->SetGraphicsRootSignature(rootsig);
        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
        command_list->RSSetViewports(1, &viewport);
        D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
        command_list->RSSetScissorRects(1, &scissor_rect);
        command_list->SetPipelineState(pso);
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto cpu_handle = devices.swapchain.GetRtvHandle();
        command_list->OMSetRenderTargets(1, &cpu_handle, true, nullptr);
        command_list->DrawInstanced(3, 1, 0, 0);
      }
      {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
      }
      command_list->ResourceBarrier(1, &barrier);
      command_list_set.ExecuteCommandLists(devices.GetCommandQueue(CommandQueueType::kGraphics), CommandQueueType::kGraphics);
    }
    devices.swapchain.Present();
    SignalQueueOnFrameEnd(&devices.command_queue, CommandQueueType::kGraphics, &signal_values.used_signal_val, &signal_values.frame_wait_signal[frame_index]);
  }
  devices.WaitAll();
  shader_resource_set.Term();
  command_list_set.Term();
  devices.Term();
}
TEST_CASE("create buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  D3D12_RESOURCE_DESC resource_desc{};
  {
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Alignment = 0;
    resource_desc.Width = swapchain_size.width;
    resource_desc.Height = swapchain_size.height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.Format = devices.swapchain.GetDxgiFormat();
    resource_desc.SampleDesc = {1, 0};
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }
  D3D12_CLEAR_VALUE clear_value{.Format = resource_desc.Format, .Color = {1.0f,1.0f,1.0f,1.0f,}};
  auto buffer_id = physical_buffers.CreatePhysicalBuffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, resource_desc, &clear_value);
  CHECK(buffer_id);
  CHECK(physical_buffers.GetPhysicalBuffer(buffer_id));
  physical_buffers.Term();
  devices.Term();
}
TEST_CASE("create cpu handle") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  DescriptorHeapSet descriptor_heaps{&memory_resource_persistent};
  CHECK(descriptor_heaps.Init(devices.GetDevice()));
  auto handle = descriptor_heaps.RetainHandle(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  CHECK(handle.ptr);
  CHECK(descriptor_heaps.RetainHandle(1, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).ptr == handle.ptr);
  CHECK(descriptor_heaps.RetainHandle(1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV).ptr);
  CHECK(descriptor_heaps.RetainHandle(1, D3D12_DESCRIPTOR_HEAP_TYPE_RTV).ptr != handle.ptr);
  descriptor_heaps.Term();
  devices.Term();
}
TEST_CASE("draw to a created buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  BufferConfig buffer_config{
    .width = swapchain_size.width,
    .height = swapchain_size.height,
    .dimension = BufferDimensionType::k2d,
    .format = BufferFormat::kR8G8B8A8Unorm,
    .state_flags = kBufferStateFlagRtv,
    .initial_state_flags = kBufferStateFlagRtv,
    .clear_value = std::array<float, 4>{1.0f,1.0f,1.0f,1.0f},
  };
  auto clear_value = GetD3d12ClearValue(buffer_config);
  auto buffer_id = physical_buffers.CreatePhysicalBuffer(D3D12_HEAP_TYPE_DEFAULT, GetInitialD3d12ResourceStateFlag(buffer_config), ConvertToD3d12ResourceDesc(buffer_config), IsClearValueValid(buffer_config) ? &clear_value : nullptr);
  DescriptorHeapSet descriptor_heaps{&memory_resource_persistent};
  CHECK(descriptor_heaps.Init(devices.GetDevice()));
  {
    auto resource = physical_buffers.GetPhysicalBuffer(buffer_id);
    auto rtv_desc = ConvertToD3d12RtvDesc(buffer_config);
    auto handle = descriptor_heaps.RetainHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    devices.GetDevice()->CreateRenderTargetView(resource, &rtv_desc, handle);
  }
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  CHECK(signal_values.used_signal_val.empty());
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  StrId pso_id("pso_tmp");
  StrId rootsig_id("rootsig_tmp");
  shader_resource_set.CreateVsPsPipelineStateObject(pso_id, devices.GetDevice(), rootsig_id, L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/test.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    command_list_set.RotateCommandAllocators();
    devices.swapchain.UpdateBackBufferIndex();
    {
      auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
      {
        auto& width = swapchain_size.width;
        auto& height = swapchain_size.height;
        command_list->SetGraphicsRootSignature(shader_resource_set.GetRootSignature(rootsig_id));
        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
        command_list->RSSetViewports(1, &viewport);
        D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
        command_list->RSSetScissorRects(1, &scissor_rect);
        command_list->SetPipelineState(shader_resource_set.GetPipelineStateObject(pso_id));
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto cpu_handle = descriptor_heaps.GetHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        command_list->OMSetRenderTargets(1, &cpu_handle, true, nullptr);
        command_list->DrawInstanced(3, 1, 0, 0);
      }
      command_list_set.ExecuteCommandLists(devices.GetCommandQueue(CommandQueueType::kGraphics), CommandQueueType::kGraphics);
    }
    devices.swapchain.Present();
    SignalQueueOnFrameEnd(&devices.command_queue, CommandQueueType::kGraphics, &signal_values.used_signal_val, &signal_values.frame_wait_signal[frame_index]);
  }
  devices.WaitAll();
  descriptor_heaps.Term();
  physical_buffers.Term();
  shader_resource_set.Term();
  command_list_set.Term();
  devices.Term();
}
TEST_CASE("cbv frame buffering") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  FrameBufferedBufferSet frame_buffered_buffers(&memory_resource_persistent);
  {
    PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
    BufferConfig buffer_config = GetBufferConfigUploadBuffer(sizeof(uint32_t));
    auto initial_flag = GetInitialD3d12ResourceStateFlag(buffer_config);
    auto d3d12_resource_desc = ConvertToD3d12ResourceDesc(buffer_config);
    vector<ID3D12Resource*> resource_list{&memory_resource_work};
    resource_list.resize(frame_buffer_num);
    for (uint32_t i = 0; i < frame_buffer_num; i++) {
      CAPTURE(i);
      auto buffer_id = physical_buffers.CreatePhysicalBuffer(D3D12_HEAP_TYPE_UPLOAD, initial_flag, d3d12_resource_desc, nullptr);
      CHECK(buffer_id);
      resource_list[i] = physical_buffers.GetPhysicalBuffer(buffer_id);
      CHECK(resource_list[i]);
    }
    auto frame_buffered_buffer_id = frame_buffered_buffers.RegisterFrameBufferedBuffers(resource_list.data(), frame_buffer_num);
    void* prev_ptr = nullptr;
    for (uint32_t i = 0; i < frame_buffer_num; i++) {
      CAPTURE(i);
      auto ptr = frame_buffered_buffers.GetFrameBufferedBuffers(frame_buffered_buffer_id, i);
      CHECK(ptr);
      CHECK(ptr != prev_ptr);
      memcpy(ptr, &i, sizeof(i));
      prev_ptr = ptr;
    }
  }
  physical_buffers.Term();
  devices.Term();
}
TEST_CASE("use cbv (change buffer color dynamically)") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_size_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  CHECK(signal_values.used_signal_val.empty());
  CHECK(signal_values.frame_wait_signal.size() == frame_buffer_num);
  for (auto& map : signal_values.frame_wait_signal) {
    CHECK(map.empty());
  }
  PmrLinearAllocator memory_resource_work(&buffer[buffer_size_in_bytes_work], buffer_size_in_bytes_work);
  auto [rootsig, pso] = shader_resource_set.CreateVsPsPipelineStateObject(StrId("pso"), devices.GetDevice(), StrId("rootsig_tmp"), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/test-cbv.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  vector<void*> cbv_ptr{&memory_resource_persistent};
  {
    BufferConfig buffer_config = GetBufferConfigUploadBuffer(sizeof(float) * 4);
    cbv_ptr.resize(frame_buffer_num);
    for (uint32_t i = 0; i < frame_buffer_num; i++) {
      CAPTURE(i);
      auto buffer_id = physical_buffers.CreatePhysicalBuffer(D3D12_HEAP_TYPE_UPLOAD, GetInitialD3d12ResourceStateFlag(buffer_config), ConvertToD3d12ResourceDesc(buffer_config), nullptr);
      D3D12_RANGE read_range{};
      auto hr = physical_buffers.GetPhysicalBuffer(buffer_id)->Map(0, &read_range, &cbv_ptr[i]);
      CHECK(SUCCEEDED(hr));
      if (FAILED(hr)) {
        logerror("cbv->Map failed. {} {}", hr, i);
      }
    }
  }
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    {
      // update cbv
      float color_val_per_channel = 0.1f * static_cast<float>(frame_no);
      float color_diff[4] = {color_val_per_channel, color_val_per_channel, color_val_per_channel, 0.0f};
      memcpy(cbv_ptr[frame_index], color_diff, sizeof(float) * 4);
    }
    command_list_set.RotateCommandAllocators();
    devices.swapchain.UpdateBackBufferIndex();
    {
      /** TODO use cbv with frame buffering
       ** create cpu handles for each buffer
       ** attach gpu handles to pipeline
       ** increment color value by 0.1f and check results from RenderDoc captureing multiple frames in sequence
       **/
      auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
      D3D12_RESOURCE_BARRIER barrier{};
      {
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = devices.swapchain.GetResource();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      command_list->ResourceBarrier(1, &barrier);
      {
        auto& width = swapchain_size.width;
        auto& height = swapchain_size.height;
        command_list->SetGraphicsRootSignature(rootsig);
        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
        command_list->RSSetViewports(1, &viewport);
        D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
        command_list->RSSetScissorRects(1, &scissor_rect);
        command_list->SetPipelineState(pso);
        command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        auto cpu_handle = devices.swapchain.GetRtvHandle();
        command_list->OMSetRenderTargets(1, &cpu_handle, true, nullptr);
        command_list->DrawInstanced(3, 1, 0, 0);
      }
      {
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
      }
      command_list->ResourceBarrier(1, &barrier);
      command_list_set.ExecuteCommandLists(devices.GetCommandQueue(CommandQueueType::kGraphics), CommandQueueType::kGraphics);
    }
    devices.swapchain.Present();
    SignalQueueOnFrameEnd(&devices.command_queue, CommandQueueType::kGraphics, &signal_values.used_signal_val, &signal_values.frame_wait_signal[frame_index]);
  }
  devices.WaitAll();
  physical_buffers.Term();
  shader_resource_set.Term();
  command_list_set.Term();
  devices.Term();
}
