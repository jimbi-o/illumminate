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
    if (command_list_in_use_.contains(command_queue_type) && command_list_in_use_.at(command_queue_type) != nullptr) {
      ASSERT(command_list_num_in_use_.at(command_queue_type) == num);
      return command_list_in_use_.at(command_queue_type);
    }
    auto allocators = allocator_.RetainCommandAllocator(command_queue_type, num);
    auto command_list = pool_.RetainCommandList(command_queue_type, num, allocators);
    allocator_buffer_[allocator_buffer_index_].push_back(allocators);
    command_list_in_use_.insert_or_assign(command_queue_type, command_list);
    command_list_num_in_use_.insert_or_assign(command_queue_type, num);
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
    command_list_in_use_.insert_or_assign(command_queue_type, nullptr);
    command_list_num_in_use_.insert_or_assign(command_queue_type, 0);
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
      SET_NAME(root_signature, L"rootsig-vsps", rootsig_id);
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
    SET_NAME(pipeline_state, L"pso-vsps", pso_id);
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
      SET_NAME(root_signature, L"rootsig-cs", rootsig_id);
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
    SET_NAME(pipeline_state, L"pso-cs", pso_id);
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
constexpr D3D12_DESCRIPTOR_HEAP_TYPE GetD3d12DescriptorHeapType(const BufferStateFlags flag) {
  if (flag & kBufferStateFlagCbvUpload) {
    return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  }
  if (flag & kBufferStateFlagSrvPsOnly) {
    return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  }
  if (flag & kBufferStateFlagSrvNonPs) {
    return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  }
  if (flag & kBufferStateFlagUav) {
    return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  }
  if (flag & kBufferStateFlagRtv) {
    return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  }
  if (flag & kBufferStateFlagDsv) {
    return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  }
  return D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
}
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
    for (auto& [buffer_id, resource] : resource_list_) {
      if (allocation_list_.contains(buffer_id)) {
        resource->Release();
      }
    }
    for (auto& [buffer_id, allocation] : allocation_list_) {
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
    SET_NAME(resource, L"resource", buffer_id_used_);
    return buffer_id_used_;
  }
  PhysicalBufferId RegisterExternalPhysicalBuffer(ID3D12Resource* resource) {
    buffer_id_used_++;
    resource_list_.emplace(buffer_id_used_, resource);
    return buffer_id_used_;
  }
  ID3D12Resource* GetPhysicalBuffer(const PhysicalBufferId buffer_id) {
    return resource_list_.at(buffer_id);
  }
  constexpr const auto& GetPhysicalBufferList() const { return resource_list_; }
 private:
  D3D12MA::Allocator* allocator_ = nullptr;
  unordered_map<PhysicalBufferId, D3D12MA::Allocation*> allocation_list_;
  unordered_map<PhysicalBufferId, ID3D12Resource*> resource_list_;
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
constexpr D3D12_RESOURCE_STATES GetInitialD3d12ResourceStateFlag(const BufferConfig& buffer_config) {
  return ConvertToD3d12ResourceState(buffer_config.initial_state_flags);
}
constexpr D3D12_RESOURCE_FLAGS ConvertToD3d12ResourceFlags(const BufferStateFlags state_flags) {
  D3D12_RESOURCE_FLAGS flags{};
  if (state_flags & kBufferStateFlagUav) flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (state_flags & kBufferStateFlagRtv) flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (state_flags & kBufferStateFlagDsv) flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
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
  if (buffer_config.state_flags & kBufferStateFlagRtv) return true;
  if (buffer_config.state_flags & kBufferStateFlagDsv) return true;
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
constexpr D3D12_DSV_FLAGS ConvertToD3d12DepthStencilFlag(const DepthStencilFlag& flag) {
  switch (flag) {
    case DepthStencilFlag::kDefault:              return D3D12_DSV_FLAG_NONE;
    case DepthStencilFlag::kDepthStencilReadOnly: return (D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL);
    case DepthStencilFlag::kDepthReadOnly:        return D3D12_DSV_FLAG_READ_ONLY_DEPTH;
    case DepthStencilFlag::kStencilReadOnly:      return D3D12_DSV_FLAG_READ_ONLY_STENCIL;
  }
  return D3D12_DSV_FLAG_NONE;
}
constexpr D3D12_DEPTH_STENCIL_VIEW_DESC ConvertToD3d12DsvDesc(const BufferConfig& buffer_config) {
  return {
    .Format = GetDxgiFormat(buffer_config.format),
    .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
    .Flags = ConvertToD3d12DepthStencilFlag(buffer_config.depth_stencil_flag),
    .Texture2D{
      .MipSlice = 0,
    },
  };
}
constexpr D3D12_SHADER_RESOURCE_VIEW_DESC ConvertToD3d12SrvDesc(const BufferConfig& buffer_config) {
  return {
    .Format = GetDxgiFormat(buffer_config.format),
    .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
    .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
    .Texture2D{
      .MostDetailedMip = 0,
      .MipLevels = 1,
      .PlaneSlice = 0,
      .ResourceMinLODClamp = 0.0f,
    },
  };
}
constexpr D3D12_UNORDERED_ACCESS_VIEW_DESC ConvertToD3d12UavDesc(const BufferConfig& buffer_config) {
  return {
    .Format = GetDxgiFormat(buffer_config.format),
    .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
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
    .state_flags = kBufferStateFlagCbvUpload,
    .initial_state_flags = kBufferStateFlagCbvUpload,
    .clear_value = {},
    .dimension = BufferDimensionType::kBuffer,
    .format = BufferFormat::kUnknown,
  };
}
class DescriptorHandleSet {
 public:
  DescriptorHandleSet(std::pmr::memory_resource* memory_resource) : map_(memory_resource) {
    map_.insert_or_assign(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, unordered_map<PhysicalBufferId, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource});
    map_.insert_or_assign(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, unordered_map<PhysicalBufferId, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource});
    map_.insert_or_assign(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, unordered_map<PhysicalBufferId, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource});
    map_.insert_or_assign(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, unordered_map<PhysicalBufferId, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource});
    ASSERT(map_.size() == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES);
  }
  void CreateSrv(const PhysicalBufferId buffer_id, const BufferConfig& buffer_config, D3d12Device* const device, ID3D12Resource* const resource, const D3D12_CPU_DESCRIPTOR_HANDLE& handle) {
    auto srv_desc = ConvertToD3d12SrvDesc(buffer_config);
    device->CreateShaderResourceView(resource, &srv_desc, handle);
    map_.at(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).insert_or_assign(buffer_id, handle);
  }
  void CreateUav(const PhysicalBufferId buffer_id, const BufferConfig& buffer_config, D3d12Device* const device, ID3D12Resource* const resource, const D3D12_CPU_DESCRIPTOR_HANDLE& handle) {
    auto uav_desc = ConvertToD3d12UavDesc(buffer_config);
    device->CreateUnorderedAccessView(resource, nullptr, &uav_desc, handle);
    map_.at(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).insert_or_assign(buffer_id, handle);
  }
  void CreateRtv(const PhysicalBufferId buffer_id, const BufferConfig& buffer_config, D3d12Device* const device, ID3D12Resource* const resource, const D3D12_CPU_DESCRIPTOR_HANDLE& handle) {
    auto rtv_desc = ConvertToD3d12RtvDesc(buffer_config);
    device->CreateRenderTargetView(resource, &rtv_desc, handle);
    map_.at(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).insert_or_assign(buffer_id, handle);
  }
  void CreateDsv(const PhysicalBufferId buffer_id, const BufferConfig& buffer_config, D3d12Device* const device, ID3D12Resource* const resource, const D3D12_CPU_DESCRIPTOR_HANDLE& handle) {
    auto dsv_desc = ConvertToD3d12DsvDesc(buffer_config);
    device->CreateDepthStencilView(resource, &dsv_desc, handle);
    map_.at(D3D12_DESCRIPTOR_HEAP_TYPE_DSV).insert_or_assign(buffer_id, handle);
  }
  D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(const PhysicalBufferId buffer_id, const D3D12_DESCRIPTOR_HEAP_TYPE type) {
    return map_.at(type).at(buffer_id);
  }
 private:
  unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, unordered_map<PhysicalBufferId, D3D12_CPU_DESCRIPTOR_HANDLE>> map_;
};
PhysicalBufferId CreatePhysicalBuffer(const BufferConfig& buffer_config, D3d12Device* const device, PhysicalBufferSet* const physical_buffers, DescriptorHeapSet* const descriptor_heaps, DescriptorHandleSet* const descriptor_handles) {
  auto clear_value = GetD3d12ClearValue(buffer_config);
  auto buffer_id = physical_buffers->CreatePhysicalBuffer(D3D12_HEAP_TYPE_DEFAULT, GetInitialD3d12ResourceStateFlag(buffer_config), ConvertToD3d12ResourceDesc(buffer_config), IsClearValueValid(buffer_config) ? &clear_value : nullptr);
  auto resource = physical_buffers->GetPhysicalBuffer(buffer_id);
  if ((buffer_config.state_flags & kBufferStateFlagSrvNonPs) || (buffer_config.state_flags & kBufferStateFlagSrvPsOnly)) {
    auto srv_handle = descriptor_heaps->RetainHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    descriptor_handles->CreateSrv(buffer_id, buffer_config, device, resource, srv_handle);
  }
  if (buffer_config.state_flags & kBufferStateFlagUav) {
    auto uav_handle = descriptor_heaps->RetainHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    descriptor_handles->CreateUav(buffer_id, buffer_config, device, resource, uav_handle);
  }
  if (buffer_config.state_flags & kBufferStateFlagRtv) {
    auto rtv_handle = descriptor_heaps->RetainHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    descriptor_handles->CreateRtv(buffer_id, buffer_config, device, resource, rtv_handle);
  }
  if (buffer_config.state_flags & kBufferStateFlagDsv) {
    auto dsv_handle = descriptor_heaps->RetainHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    descriptor_handles->CreateDsv(buffer_id, buffer_config, device, resource, dsv_handle);
  }
  return buffer_id;
}
using FrameBufferedBufferId = uint32_t;
class FrameBufferedBufferSet {
 public:
  FrameBufferedBufferSet(std::pmr::memory_resource* memory_resource) : mapped_ptr_(memory_resource), memory_resource_(memory_resource) {
  }
  FrameBufferedBufferId RegisterFrameBufferedBuffers(D3d12Device* const device, const uint32_t buffer_size_in_bytes, ID3D12Resource** resource_list, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handles, const uint32_t resource_list_len) {
    buffer_id_used_++;
    mapped_ptr_.emplace(buffer_id_used_, vector<void*>{memory_resource_});
    auto& ptr_list = mapped_ptr_.at(buffer_id_used_);
    ptr_list.resize(resource_list_len);
    cpu_handles_.emplace(buffer_id_used_, vector<D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource_});
    D3D12_RANGE read_range{};
    for (uint32_t i = 0; i < resource_list_len; i++) {
      auto hr = resource_list[i]->Map(0, &read_range, &ptr_list[i]);
      if (FAILED(hr)) {
        return {};
      }
      D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc{resource_list[i]->GetGPUVirtualAddress(), buffer_size_in_bytes};
      device->CreateConstantBufferView(&cbv_desc, cpu_handles[i]);
    }
    auto& handle_list = cpu_handles_.at(buffer_id_used_);
    handle_list.resize(resource_list_len);
    handle_list.assign(cpu_handles, cpu_handles + resource_list_len);
    return buffer_id_used_;
  }
  void* GetFrameBufferedBuffer(FrameBufferedBufferId buffer_id, const uint32_t index) {
    return mapped_ptr_.at(buffer_id)[index];
  }
  D3D12_CPU_DESCRIPTOR_HANDLE& GetFrameBufferedBufferCpuHandle(FrameBufferedBufferId buffer_id, const uint32_t index) {
    return cpu_handles_.at(buffer_id)[index];
  }
 private:
  unordered_map<FrameBufferedBufferId, vector<void*>> mapped_ptr_;
  unordered_map<FrameBufferedBufferId, vector<D3D12_CPU_DESCRIPTOR_HANDLE>> cpu_handles_;
  std::pmr::memory_resource* memory_resource_;
  FrameBufferedBufferId buffer_id_used_ = 0;
};
FrameBufferedBufferId CreateFrameBufferedConstantBuffers(D3d12Device* const device, const uint32_t buffer_size_in_bytes, const uint32_t frame_buffer_num, PhysicalBufferSet* physical_buffers, DescriptorHeapSet* descriptor_heaps, FrameBufferedBufferSet* frame_buffered_buffers, std::pmr::memory_resource* memory_resource_work) {
  uint32_t mask = 255;
  auto buffer_size_in_bytes_256_aligned = (buffer_size_in_bytes + mask) & ~mask;
  BufferConfig buffer_config = GetBufferConfigUploadBuffer(buffer_size_in_bytes_256_aligned);
  auto initial_flag = GetInitialD3d12ResourceStateFlag(buffer_config);
  auto d3d12_resource_desc = ConvertToD3d12ResourceDesc(buffer_config);
  vector<ID3D12Resource*> resource_list{memory_resource_work};
  resource_list.resize(frame_buffer_num);
  vector<D3D12_CPU_DESCRIPTOR_HANDLE> cpu_handles{memory_resource_work};
  cpu_handles.resize(frame_buffer_num);
  for (uint32_t i = 0; i < frame_buffer_num; i++) {
    auto buffer_id = physical_buffers->CreatePhysicalBuffer(D3D12_HEAP_TYPE_UPLOAD, initial_flag, d3d12_resource_desc, nullptr);
    resource_list[i] = physical_buffers->GetPhysicalBuffer(buffer_id);
    auto cpu_handle = descriptor_heaps->RetainHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    cpu_handles[i] = cpu_handle;
  }
  auto frame_buffered_buffer_id = frame_buffered_buffers->RegisterFrameBufferedBuffers(device, buffer_size_in_bytes_256_aligned, resource_list.data(), cpu_handles.data(), frame_buffer_num);
  return frame_buffered_buffer_id;
}
constexpr D3D12_RESOURCE_BARRIER_FLAGS ConvertToD3d12SplitType(const BarrierSplitType& type) {
  switch (type) {
    case BarrierSplitType::kNone:  return D3D12_RESOURCE_BARRIER_FLAG_NONE;
    case BarrierSplitType::kBegin: return D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    case BarrierSplitType::kEnd:   return D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
  }
  return D3D12_RESOURCE_BARRIER_FLAG_NONE;
}
constexpr D3D12_RESOURCE_BARRIER ConvertToD3d12Barrier(const BarrierConfig& barrier, ID3D12Resource* resource) {
  switch (barrier.params.index()) {
    case 0: {
      return {
        .Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = ConvertToD3d12SplitType(barrier.split_type),
        .Transition = {
          .pResource   = resource,
          .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          .StateBefore = ConvertToD3d12ResourceState(std::get<BarrierTransition>(barrier.params).state_before),
          .StateAfter  = ConvertToD3d12ResourceState(std::get<BarrierTransition>(barrier.params).state_after),
        }
      };
    }
    case 1: {
      return {
        .Type  = D3D12_RESOURCE_BARRIER_TYPE_UAV,
        .Flags = ConvertToD3d12SplitType(barrier.split_type),
        .UAV = {.pResource = resource,}
      };
    }
  }
  // TODO alias
  return {};
}
vector<vector<D3D12_RESOURCE_BARRIER>> PrepareD3d12ResourceBarriers(const vector<vector<BarrierConfig>>& barriers, const unordered_map<BufferId, PhysicalBufferId>& buffer_id_map, const unordered_map<BufferId, ID3D12Resource*>& resource_list, std::pmr::memory_resource* memory_resource) {
  vector<vector<D3D12_RESOURCE_BARRIER>> d3d12_barriers{memory_resource};
  d3d12_barriers.reserve(barriers.size());
  for (auto& barriers_per_pass : barriers) {
    d3d12_barriers.push_back(vector<D3D12_RESOURCE_BARRIER>{memory_resource});
    d3d12_barriers.back().reserve(barriers_per_pass.size());
    for (auto& barrier_config : barriers_per_pass) {
      auto& buffer_id = barrier_config.buffer_id;
      ID3D12Resource* resource = nullptr;
      if (buffer_id_map.contains(buffer_id) && resource_list.contains(buffer_id_map.at(buffer_id))) {
        resource = resource_list.at(buffer_id_map.at(buffer_id));
      }
      d3d12_barriers.back().push_back(ConvertToD3d12Barrier(barrier_config, resource));
    }
  }
  return d3d12_barriers;
}
void ExecuteBarriers(vector<D3D12_RESOURCE_BARRIER>& d3d12_barriers, const vector<BarrierConfig>& barriers, unordered_map<BufferId, ID3D12Resource*>& external_buffer_resources, D3d12CommandList* command_list) {
  if (barriers.empty()) return;
  auto barrier_num = static_cast<uint32_t>(barriers.size());
  for (uint32_t i = 0; i < barrier_num; i++) {
    if (external_buffer_resources.contains(barriers[i].buffer_id)) {
      auto resource = external_buffer_resources.at(barriers[i].buffer_id);
      switch (barriers[i].params.index()) {
        default:
        case 0:
          d3d12_barriers[i].Transition.pResource = resource;
          break;
        case 1:
          d3d12_barriers[i].UAV.pResource = resource;
          break;
      }
      // TODO alias
    }
  }
  command_list->ResourceBarrier(barrier_num, d3d12_barriers.data());
}
#if 0
std::tuple<vector<vector<BufferId>>, vector<vector<D3D12_RESOURCE_BARRIER>>> ConfigureD3d12Barrier(const RenderPassBufferInfoList& pass_buffer_info_list, std::pmr::memory_resource* memory_resource_barrier, std::pmr::memory_resource* memory_resource_work) {
  auto barriers = ConfigureBarrier(pass_buffer_info_list, memory_resource_work, memory_resource_work);
  vector<vector<BufferId>> barriers_buffer_id_list;
  barriers_buffer_id_list.resize(barriers.size());
  vector<vector<D3D12_RESOURCE_BARRIER>> d3d12_barriers{memory_resource_barrier};
  d3d12_barriers.resize(barriers.size());
  for (uint32_t pass_index = 0; pass_index < barriers.size(); pass_index++) {
    barriers_buffer_id_list[pass_index].reserve(barriers[pass_index].size());
    d3d12_barriers[pass_index].reserve(barriers[pass_index].size());
    for (uint32_t i = 0; i < barriers[pass_index].size(); i++) {
      barriers_buffer_id_list[pass_index].push_back(barriers[pass_index][i].buffer_id);
      d3d12_barriers[pass_index].push_back(ConvertToD3d12Barrier(barriers[pass_index][i], nullptr));
    }
  }
  return {barriers_buffer_id_list, d3d12_barriers};
}
void ExecuteBarriers(const vector<D3D12_RESOURCE_BARRIER>& barriers, D3d12CommandList* command_list) {
  command_list->ResourceBarrier(barriers.size(), barriers.data());
}
#endif
}
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_offset_in_bytes_persistent = 0;
const uint32_t buffer_size_in_bytes_persistent = 16 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_persistent + buffer_size_in_bytes_persistent;
const uint32_t buffer_size_in_bytes_scene = 8 * 1024;
const uint32_t buffer_offset_in_bytes_frame = buffer_offset_in_bytes_scene + buffer_size_in_bytes_scene;
const uint32_t buffer_size_in_bytes_frame = 4 * 1024;
const uint32_t buffer_offset_in_bytes_work = buffer_offset_in_bytes_frame + buffer_size_in_bytes_frame;
const uint32_t buffer_size_in_bytes_work = 16 * 1024;
std::byte buffer[buffer_offset_in_bytes_work + buffer_size_in_bytes_work]{};
const uint32_t kTestFrameNum = 10;
static void BuildRenderGraphUseComputeQueue(const uint32_t primary_buffer_width, const uint32_t primary_buffer_height, const uint32_t swapchain_buffer_width, const uint32_t swapchain_buffer_height, std::pmr::memory_resource* memory_resource_work, illuminate::gfx::RenderGraph* render_graph) {
  using namespace illuminate::gfx;
  RenderGraphConfig render_graph_config(memory_resource_work);
  render_graph_config.SetPrimaryBufferSize(primary_buffer_width, primary_buffer_height);
  render_graph_config.SetSwapchainBufferSize(swapchain_buffer_width, swapchain_buffer_height);
  auto pass_draw = render_graph_config.CreateNewRenderPass({.pass_name = StrId("draw"), .command_queue_type = CommandQueueType::kCompute,});
  auto pass_copy = render_graph_config.CreateNewRenderPass({.pass_name = StrId("copy"), .command_queue_type = CommandQueueType::kGraphics,});
  render_graph_config.AppendRenderPassBufferConfig(pass_draw, {.buffer_name = StrId("mainbuffer"), .state = kBufferStateFlagUav, .read_write_flag = kWriteFlag,});
  render_graph_config.AppendRenderPassBufferConfig(pass_copy, {.buffer_name = StrId("mainbuffer"), .state = kBufferStateFlagUav, .read_write_flag = kReadFlag,});
  render_graph_config.AppendRenderPassBufferConfig(pass_copy, {.buffer_name = StrId("swapchain"),  .state = kBufferStateFlagRtv, .read_write_flag = kWriteFlag, });
  render_graph_config.AddBufferInitialState(StrId("swapchain"), kBufferStateFlagPresent);
  render_graph_config.AddBufferFinalState(StrId("swapchain"), kBufferStateFlagPresent);
  render_graph->Build(render_graph_config, memory_resource_work);
}
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  CHECK(shader_resource_set.Init(devices.GetDevice()));
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
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
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  BufferConfig buffer_config{
    .width = swapchain_size.width,
    .height = swapchain_size.height,
    .state_flags = kBufferStateFlagRtv,
    .initial_state_flags = kBufferStateFlagRtv,
    .clear_value = std::array<float, 4>{1.0f,1.0f,1.0f,1.0f},
    .dimension = BufferDimensionType::k2d,
    .format = BufferFormat::kR8G8B8A8Unorm,
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
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  DescriptorHeapSet descriptor_heaps{&memory_resource_persistent};
  CHECK(descriptor_heaps.Init(devices.GetDevice()));
  FrameBufferedBufferSet frame_buffered_buffers(&memory_resource_persistent);
  {
    PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
    const uint32_t cbv_size_in_bytes = 256;
    BufferConfig buffer_config = GetBufferConfigUploadBuffer(cbv_size_in_bytes);
    auto initial_flag = GetInitialD3d12ResourceStateFlag(buffer_config);
    auto d3d12_resource_desc = ConvertToD3d12ResourceDesc(buffer_config);
    vector<ID3D12Resource*> resource_list{&memory_resource_work};
    resource_list.resize(frame_buffer_num);
    vector<D3D12_CPU_DESCRIPTOR_HANDLE> cpu_handles{&memory_resource_work};
    cpu_handles.resize(frame_buffer_num);
    for (uint32_t i = 0; i < frame_buffer_num; i++) {
      CAPTURE(i);
      auto buffer_id = physical_buffers.CreatePhysicalBuffer(D3D12_HEAP_TYPE_UPLOAD, initial_flag, d3d12_resource_desc, nullptr);
      CHECK(buffer_id);
      resource_list[i] = physical_buffers.GetPhysicalBuffer(buffer_id);
      CHECK(resource_list[i]);
      auto cpu_handle = descriptor_heaps.RetainHandle(buffer_id, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      cpu_handles[i] = cpu_handle;
    }
    auto frame_buffered_buffer_id = frame_buffered_buffers.RegisterFrameBufferedBuffers(devices.GetDevice(), cbv_size_in_bytes, resource_list.data(), cpu_handles.data(), frame_buffer_num);
    void* prev_ptr = nullptr;
    for (uint32_t i = 0; i < frame_buffer_num; i++) {
      CAPTURE(i);
      auto ptr = frame_buffered_buffers.GetFrameBufferedBuffer(frame_buffered_buffer_id, i);
      CHECK(ptr);
      CHECK(ptr != prev_ptr);
      memcpy(ptr, &i, sizeof(i));
      prev_ptr = ptr;
    }
  }
  descriptor_heaps.Term();
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
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  DescriptorHeapSet descriptor_heaps{&memory_resource_persistent};
  CHECK(descriptor_heaps.Init(devices.GetDevice()));
  ShaderVisibleDescriptorHeap shader_visible_descriptor_heap;
  CHECK(shader_visible_descriptor_heap.Init(devices.GetDevice()));
  FrameBufferedBufferSet frame_buffered_buffers(&memory_resource_persistent);
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  auto [rootsig, pso] = shader_resource_set.CreateVsPsPipelineStateObject(StrId("pso"), devices.GetDevice(), StrId("rootsig_tmp"), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/test-cbv.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  auto cbv_id = CreateFrameBufferedConstantBuffers(devices.GetDevice(), sizeof(float) * 4, frame_buffer_num, &physical_buffers, &descriptor_heaps, &frame_buffered_buffers, &memory_resource_work);
  CHECK(cbv_id);
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    CAPTURE(frame_no);
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    {
      // update cbv
      float color_val_per_channel = 0.1f * static_cast<float>(frame_no);
      float color_diff[4] = {color_val_per_channel, color_val_per_channel, color_val_per_channel, 0.0f};
      auto cbv_ptr = frame_buffered_buffers.GetFrameBufferedBuffer(cbv_id, frame_index);
      CHECK(cbv_ptr);
      memcpy(cbv_ptr, color_diff, sizeof(float) * 4);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE cbv_gpu_handle{};
    {
      auto cpu_handle = frame_buffered_buffers.GetFrameBufferedBufferCpuHandle(cbv_id, frame_index);
      cbv_gpu_handle = shader_visible_descriptor_heap.CopyToBufferDescriptorHeap(&cpu_handle, 1, &memory_resource_work);
      memory_resource_work.Reset();
    }
    command_list_set.RotateCommandAllocators();
    devices.swapchain.UpdateBackBufferIndex();
    {
      auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
      shader_visible_descriptor_heap.SetDescriptorHeapsToCommandList(command_list);
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
        command_list->SetGraphicsRootDescriptorTable(0, cbv_gpu_handle);
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
  shader_visible_descriptor_heap.Term();
  descriptor_heaps.Term();
  physical_buffers.Term();
  shader_resource_set.Term();
  command_list_set.Term();
  devices.Term();
}
TEST_CASE("load from srv") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  DescriptorHeapSet descriptor_heaps(&memory_resource_persistent);
  CHECK(descriptor_heaps.Init(devices.GetDevice()));
  DescriptorHandleSet descriptor_handles(&memory_resource_persistent);
  ShaderVisibleDescriptorHeap shader_visible_descriptor_heap;
  CHECK(shader_visible_descriptor_heap.Init(devices.GetDevice()));
  FrameBufferedBufferSet frame_buffered_buffers(&memory_resource_persistent);
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  auto [draw_rootsig, draw_pso] = shader_resource_set.CreateVsPsPipelineStateObject(StrId("draw_pso"), devices.GetDevice(), StrId("draw_rootsig"), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/test-cbv.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  auto [copy_rootsig, copy_pso] = shader_resource_set.CreateVsPsPipelineStateObject(StrId("copy_pso"), devices.GetDevice(), StrId("copy_rootsig"), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/copysrv.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  auto cbv_id = CreateFrameBufferedConstantBuffers(devices.GetDevice(), sizeof(float) * 4, frame_buffer_num, &physical_buffers, &descriptor_heaps, &frame_buffered_buffers, &memory_resource_work);
  CHECK(cbv_id);
  auto rtv_id = CreatePhysicalBuffer({
      .width = swapchain_size.width,
      .height = swapchain_size.height,
      .state_flags = static_cast<BufferStateFlags>(kBufferStateFlagRtv | kBufferStateFlagSrvPsOnly),
      .initial_state_flags = kBufferStateFlagRtv,
      .clear_value = std::array<float, 4>{1.0f,1.0f,1.0f,1.0f},
      .dimension = BufferDimensionType::k2d,
      .format = BufferFormat::kR8G8B8A8Unorm,
    },
    devices.GetDevice(), &physical_buffers, &descriptor_heaps, &descriptor_handles);
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    CAPTURE(frame_no);
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    // update cbv
    {
      float color_val_per_channel = 0.1f * static_cast<float>(frame_no);
      float color_diff[4] = {color_val_per_channel, color_val_per_channel, color_val_per_channel, 0.0f};
      auto cbv_ptr = frame_buffered_buffers.GetFrameBufferedBuffer(cbv_id, frame_index);
      CHECK(cbv_ptr);
      memcpy(cbv_ptr, color_diff, sizeof(float) * 4);
    }
    // copy descriptor handles to gpu side
    D3D12_GPU_DESCRIPTOR_HANDLE cbv_gpu_handle{};
    {
      auto cpu_handle = frame_buffered_buffers.GetFrameBufferedBufferCpuHandle(cbv_id, frame_index);
      cbv_gpu_handle = shader_visible_descriptor_heap.CopyToBufferDescriptorHeap(&cpu_handle, 1, &memory_resource_work);
      memory_resource_work.Reset();
    }
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_handle{};
    {
      auto srv_handle = descriptor_handles.GetCpuHandle(rtv_id, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
      srv_gpu_handle = shader_visible_descriptor_heap.CopyToBufferDescriptorHeap(&srv_handle, 1, &memory_resource_work);
      memory_resource_work.Reset();
    }
    devices.swapchain.UpdateBackBufferIndex();
    command_list_set.RotateCommandAllocators();
    auto command_list = command_list_set.GetCommandList(CommandQueueType::kGraphics, 1)[0];
    shader_visible_descriptor_heap.SetDescriptorHeapsToCommandList(command_list);
    // barrier
    {
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.pResource   = devices.swapchain.GetResource();
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
      barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      command_list->ResourceBarrier(1, &barrier);
    }
    // draw pass
    {
      auto& width = swapchain_size.width;
      auto& height = swapchain_size.height;
      command_list->SetGraphicsRootSignature(draw_rootsig);
      D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
      command_list->RSSetViewports(1, &viewport);
      D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
      command_list->RSSetScissorRects(1, &scissor_rect);
      command_list->SetPipelineState(draw_pso);
      command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      auto rtv_handle = descriptor_handles.GetCpuHandle(rtv_id, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
      command_list->OMSetRenderTargets(1, &rtv_handle, true, nullptr);
      command_list->SetGraphicsRootDescriptorTable(0, cbv_gpu_handle);
      command_list->DrawInstanced(3, 1, 0, 0);
    }
    // barrier
    {
      D3D12_RESOURCE_BARRIER barriers[2]{};
      {
        auto& barrier = barriers[0];
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = devices.swapchain.GetResource();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      {
        auto& barrier = barriers[1];
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = physical_buffers.GetPhysicalBuffer(rtv_id);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
      }
      command_list->ResourceBarrier(2, barriers);
    }
    // copy pass
    {
      auto& width = swapchain_size.width;
      auto& height = swapchain_size.height;
      command_list->SetGraphicsRootSignature(copy_rootsig);
      D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
      command_list->RSSetViewports(1, &viewport);
      D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
      command_list->RSSetScissorRects(1, &scissor_rect);
      command_list->SetPipelineState(copy_pso);
      command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      auto swapchain_cpu_handle = devices.swapchain.GetRtvHandle();
      command_list->OMSetRenderTargets(1, &swapchain_cpu_handle, true, nullptr);
      command_list->SetGraphicsRootDescriptorTable(0, srv_gpu_handle);
      command_list->DrawInstanced(3, 1, 0, 0);
    }
    // barrier
    {
      D3D12_RESOURCE_BARRIER barriers[2]{};
      {
        auto& barrier = barriers[0];
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = devices.swapchain.GetResource();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;
      }
      {
        auto& barrier = barriers[1];
        barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.pResource   = physical_buffers.GetPhysicalBuffer(rtv_id);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
      }
      command_list->ResourceBarrier(2, barriers);
    }
    command_list_set.ExecuteCommandLists(devices.GetCommandQueue(CommandQueueType::kGraphics), CommandQueueType::kGraphics);
    devices.swapchain.Present();
    SignalQueueOnFrameEnd(&devices.command_queue, CommandQueueType::kGraphics, &signal_values.used_signal_val, &signal_values.frame_wait_signal[frame_index]);
  }
  devices.WaitAll();
  shader_visible_descriptor_heap.Term();
  descriptor_heaps.Term();
  physical_buffers.Term();
  shader_resource_set.Term();
  command_list_set.Term();
  devices.Term();
}
TEST_CASE("use compute queue") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1920, 1080};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistent(&buffer[buffer_offset_in_bytes_persistent], buffer_size_in_bytes_persistent);
  PmrLinearAllocator memory_resource_scene(&buffer[buffer_offset_in_bytes_scene], buffer_size_in_bytes_scene);
  CommandListSet command_list_set(&memory_resource_persistent);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  ShaderResourceSet shader_resource_set(&memory_resource_persistent);
  shader_resource_set.Init(devices.GetDevice());
  PhysicalBufferSet physical_buffers(&memory_resource_persistent);
  CHECK(physical_buffers.Init(devices.GetDevice(), devices.dxgi_core.GetAdapter()));
  DescriptorHeapSet descriptor_heaps(&memory_resource_persistent);
  CHECK(descriptor_heaps.Init(devices.GetDevice()));
  DescriptorHandleSet descriptor_handles(&memory_resource_persistent);
  ShaderVisibleDescriptorHeap shader_visible_descriptor_heap;
  CHECK(shader_visible_descriptor_heap.Init(devices.GetDevice()));
  FrameBufferedBufferSet frame_buffered_buffers(&memory_resource_persistent);
  SignalValues signal_values(&memory_resource_persistent, frame_buffer_num);
  PmrLinearAllocator memory_resource_work(&buffer[buffer_offset_in_bytes_work], buffer_size_in_bytes_work);
  auto [compute_queue_rootsig, compute_queue_pso] = shader_resource_set.CreateCsPipelineStateObject(StrId("compute_queue_pso"), devices.GetDevice(), StrId("draw_rootsig"), L"shader/test/fill-screen.cs.hlsl", &memory_resource_work);
  auto [copy_rootsig, copy_pso] = shader_resource_set.CreateVsPsPipelineStateObject(StrId("copy_pso"), devices.GetDevice(), StrId("copy_rootsig"), L"shader/test/fullscreen-triangle.vs.hlsl", L"shader/test/copyuav.ps.hlsl", {{devices.swapchain.GetDxgiFormat()}, 1}, ShaderResourceSet::DepthStencilEnableFlag::kDisabled, &memory_resource_work);
  memory_resource_work.Reset();
  using RenderPassFunction = std::function<void(D3d12CommandList*, const BufferId*, const unordered_map<BufferId, BufferConfig>&, ID3D12Resource**, D3D12_CPU_DESCRIPTOR_HANDLE*, D3D12_GPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE)>;
  using RenderPassFunctionList = vector<RenderPassFunction>;
  RenderPassFunctionList render_pass_function_list{&memory_resource_persistent};
  {
    render_pass_function_list.push_back([compute_queue_rootsig, compute_queue_pso](D3d12CommandList* command_list, const BufferId* buffer_id_list, const unordered_map<BufferId, BufferConfig>& buffer_config_list, ID3D12Resource** resource, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_buffers, [[maybe_unused]] D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_samplers) {
      const uint32_t uav_index = 0;
      command_list->DiscardResource(resource[uav_index], nullptr);
      command_list->SetComputeRootSignature(compute_queue_rootsig);
      command_list->SetPipelineState(compute_queue_pso);
      command_list->SetComputeRootDescriptorTable(0, gpu_handle_buffers);
      auto& buffer_config_0 = buffer_config_list.at(buffer_id_list[uav_index]);
      command_list->Dispatch(buffer_config_0.width, buffer_config_0.height, 1);
    });
    render_pass_function_list.push_back([copy_rootsig, copy_pso](D3d12CommandList* command_list, const BufferId* buffer_id_list, const unordered_map<BufferId, BufferConfig>& buffer_config_list, ID3D12Resource** resource, [[maybe_unused]] D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_buffers, [[maybe_unused]] D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle_samplers) {
      command_list->SetGraphicsRootSignature(copy_rootsig);
      const uint32_t uav_index = 0;
      const uint32_t rtv_index = 1;
      auto& rtv_config = buffer_config_list.at(buffer_id_list[rtv_index]);
      const auto& width = rtv_config.width;
      const auto& height = rtv_config.height;
      D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH};
      command_list->RSSetViewports(1, &viewport);
      D3D12_RECT scissor_rect{0L, 0L, static_cast<LONG>(width), static_cast<LONG>(height)};
      command_list->RSSetScissorRects(1, &scissor_rect);
      command_list->SetPipelineState(copy_pso);
      command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      command_list->OMSetRenderTargets(1, &cpu_handle[rtv_index], true, nullptr);
      command_list->SetGraphicsRootDescriptorTable(0, gpu_handle_buffers);
      command_list->DrawInstanced(3, 1, 0, 0);
    });
  }
  RenderGraph render_graph(&memory_resource_scene);
  BuildRenderGraphUseComputeQueue(swapchain_size.width, swapchain_size.height, devices.swapchain.GetWidth(), devices.swapchain.GetHeight(), &memory_resource_work, &render_graph);
  memory_resource_work.Reset();
  auto swapchain_buffer_id = *render_graph.GetBufferId(StrId("swapchain"), &memory_resource_work).begin(); // ret size should be 1.
  unordered_map<BufferId, PhysicalBufferId> buffer_id_map{&memory_resource_work};
  for (auto& [id, buffer_config] : render_graph.GetBufferConfigList()) {
    if (id != swapchain_buffer_id) {
      buffer_id_map.insert_or_assign(id, CreatePhysicalBuffer(buffer_config, devices.GetDevice(), &physical_buffers, &descriptor_heaps, &descriptor_handles));
    }
  }
  auto [barriers_pre_pass, barriers_post_pass] = ConfigureBarriers(render_graph, &memory_resource_scene);
  auto d3d12_barriers_pre_pass  = PrepareD3d12ResourceBarriers(barriers_pre_pass,  buffer_id_map, physical_buffers.GetPhysicalBufferList(), &memory_resource_scene);
  auto d3d12_barriers_post_pass = PrepareD3d12ResourceBarriers(barriers_post_pass, buffer_id_map, physical_buffers.GetPhysicalBufferList(), &memory_resource_scene);
  auto queue_signals = ConfigureQueueSignals(render_graph, &memory_resource_scene, &memory_resource_work);
  memory_resource_work.Reset();
  // TODO memory aliasing
  unordered_map<BufferId, ID3D12Resource*> external_buffer_resources{&memory_resource_scene};
  external_buffer_resources.insert_or_assign(swapchain_buffer_id, nullptr);
  unordered_map<BufferId, unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, D3D12_CPU_DESCRIPTOR_HANDLE>> external_cpu_handles{&memory_resource_scene};
  external_cpu_handles.insert_or_assign(swapchain_buffer_id, unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, D3D12_CPU_DESCRIPTOR_HANDLE>{&memory_resource_scene});
  external_cpu_handles.at(swapchain_buffer_id).insert_or_assign(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, D3D12_CPU_DESCRIPTOR_HANDLE{});
  struct QueueSignalInfo {
    uint64_t signal_val;
    CommandQueueType command_queue_type;
    std::byte _pad[3]{};
  };
  unordered_map<uint32_t, QueueSignalInfo> signal_queue_waiting_render_pass_list{&memory_resource_persistent};
  for (uint32_t frame_no = 0; frame_no < kTestFrameNum; frame_no++) {
    CAPTURE(frame_no);
    auto frame_index = frame_no % frame_buffer_num;
    devices.command_queue.WaitOnCpu(signal_values.frame_wait_signal[frame_index]);
    devices.swapchain.UpdateBackBufferIndex();
    external_buffer_resources.insert_or_assign(swapchain_buffer_id, devices.swapchain.GetResource());
    external_cpu_handles.at(swapchain_buffer_id).insert_or_assign(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, devices.swapchain.GetRtvHandle());
    command_list_set.RotateCommandAllocators();
    D3d12CommandList* prev_command_list = nullptr;
    for (uint32_t pass_index = 0; pass_index < render_graph.GetRenderPassNum(); pass_index++) {
      auto& command_queue_type = render_graph.GetRenderPassCommandQueueTypeList()[pass_index];
      memory_resource_work.Reset();
      if (signal_queue_waiting_render_pass_list.contains(pass_index)) {
        auto& signal_info =  signal_queue_waiting_render_pass_list.at(pass_index);
        devices.command_queue.RegisterWaitOnQueue(signal_info.command_queue_type, signal_info.signal_val, command_queue_type);
        signal_queue_waiting_render_pass_list.erase(pass_index);
      }
      auto command_list = command_list_set.GetCommandList(command_queue_type, 1)[0];
      ExecuteBarriers(d3d12_barriers_pre_pass[pass_index], barriers_pre_pass[pass_index], external_buffer_resources, command_list);
      if (prev_command_list != command_list) {
        prev_command_list = command_list;
        shader_visible_descriptor_heap.SetDescriptorHeapsToCommandList(command_list);
      }
      auto& current_render_pass_buffer_id_list = render_graph.GetRenderPassBufferIdList()[pass_index];
      auto& current_render_pass_buffer_state_list = render_graph.GetRenderPassBufferStateFlagList()[pass_index];
      vector<ID3D12Resource*> resources{&memory_resource_work};
      resources.reserve(current_render_pass_buffer_id_list.size());
      vector<D3D12_CPU_DESCRIPTOR_HANDLE> cpu_handles{&memory_resource_work};
      cpu_handles.reserve(current_render_pass_buffer_id_list.size());
      vector<D3D12_CPU_DESCRIPTOR_HANDLE> cpu_handles_buffer{&memory_resource_work};
      cpu_handles_buffer.reserve(current_render_pass_buffer_id_list.size());
      for (uint32_t i = 0; i < current_render_pass_buffer_id_list.size(); i++) {
        auto& buffer_id = current_render_pass_buffer_id_list[i];
        if (external_buffer_resources.contains(buffer_id)) {
          resources.push_back(external_buffer_resources.at(buffer_id));
        } else {
          resources.push_back(physical_buffers.GetPhysicalBuffer(buffer_id_map.at(buffer_id)));
        }
        auto d3d12_descriptor_heap_type = GetD3d12DescriptorHeapType(current_render_pass_buffer_state_list[i]);
        if (d3d12_descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES) {
          cpu_handles.push_back(D3D12_CPU_DESCRIPTOR_HANDLE{});
          continue;
        }
        auto cpu_handle = (external_cpu_handles.contains(buffer_id) && external_cpu_handles.at(buffer_id).contains(d3d12_descriptor_heap_type)) ? 
            external_cpu_handles.at(buffer_id).at(d3d12_descriptor_heap_type) : descriptor_handles.GetCpuHandle(buffer_id_map.at(buffer_id), d3d12_descriptor_heap_type);
        cpu_handles.push_back(cpu_handle);
        if (d3d12_descriptor_heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
          cpu_handles_buffer.push_back(cpu_handle);
        }
      }
      auto gpu_handle_buffer = cpu_handles_buffer.empty() ? D3D12_GPU_DESCRIPTOR_HANDLE{} : shader_visible_descriptor_heap.CopyToBufferDescriptorHeap(cpu_handles_buffer.data(), cpu_handles_buffer.size(), &memory_resource_work);
      render_pass_function_list[pass_index](command_list,
                                            current_render_pass_buffer_id_list.data(),
                                            render_graph.GetBufferConfigList(),
                                            resources.data(),
                                            cpu_handles.data(),
                                            gpu_handle_buffer,
                                            D3D12_GPU_DESCRIPTOR_HANDLE{}); // samplers
      ExecuteBarriers(d3d12_barriers_post_pass[pass_index], barriers_post_pass[pass_index], external_buffer_resources, command_list);
      if (queue_signals.contains(pass_index)) {
        command_list_set.ExecuteCommandLists(devices.GetCommandQueue(command_queue_type), command_queue_type);
        auto& signal_val = ++signal_values.used_signal_val[command_queue_type];
        devices.command_queue.RegisterSignal(command_queue_type, signal_val);
        for (auto& dst_pass : queue_signals.at(pass_index)) {
          signal_queue_waiting_render_pass_list.insert_or_assign(dst_pass, QueueSignalInfo{signal_val, command_queue_type});
        }
      }
    }
    devices.swapchain.Present();
    signal_values.frame_wait_signal[frame_index][CommandQueueType::kGraphics] = signal_values.used_signal_val[CommandQueueType::kGraphics];
  }
  devices.WaitAll();
  shader_visible_descriptor_heap.Term();
  descriptor_heaps.Term();
  physical_buffers.Term();
  shader_resource_set.Term();
  command_list_set.Term();
  devices.Term();
}
/* TODO
 * intra-frame async compute
 * inter-frame async compute
 * combine buffer flag test
 * multi threaded
 */
