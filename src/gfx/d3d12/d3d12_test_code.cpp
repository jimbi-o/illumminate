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
#include "gfx/win32/win32_window.h"
#include "gfx/render_graph.h"
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include "D3D12MemAlloc.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "doctest/doctest.h"
#include "d3dx12.h"
namespace {
using namespace illuminate::gfx;
using namespace illuminate::gfx::d3d12;
constexpr D3D12_RESOURCE_BARRIER_FLAGS ConvertToD3d12BarrierSplitFlag(const BarrierSplitType& split_type) {
  switch (split_type) {
    case BarrierSplitType::kBegin: return D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    case BarrierSplitType::kEnd:   return D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    case BarrierSplitType::kNone:  return D3D12_RESOURCE_BARRIER_FLAG_NONE;
  }
}
constexpr D3D12_RESOURCE_STATES ConvertToD3d12ResourceState(const BufferStateFlags& flags) {
  D3D12_RESOURCE_STATES state{};
  if (flags & kBufferStateFlagCbv) {
    state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
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
void ExecuteBarrier(const std::pmr::vector<BarrierConfig>& barrier_info_list, const std::pmr::unordered_map<BufferId, ID3D12Resource*>& physical_buffer, D3d12CommandList* const command_list, BufferStateList* const current_buffer_state_list, std::pmr::memory_resource* memory_resource) {
  const auto barrier_num = static_cast<uint32_t>(barrier_info_list.size());
  std::pmr::vector<D3D12_RESOURCE_BARRIER> barriers{memory_resource};
  barriers.reserve(barrier_num);
  for (auto& barrier_info : barrier_info_list) {
    barriers.push_back({});
    auto& barrier = barriers.back();
    barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = ConvertToD3d12BarrierSplitFlag(barrier_info.split_type);
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.pResource   = physical_buffer.at(barrier_info.buffer_id);
    barrier.Transition.StateBefore = ConvertToD3d12ResourceState(barrier_info.state_flag_before_pass);
    barrier.Transition.StateAfter  = ConvertToD3d12ResourceState(barrier_info.state_flag_after_pass);
    current_buffer_state_list->insert_or_assign(barrier_info.buffer_id, barrier_info.state_flag_after_pass);
  }
  command_list->ResourceBarrier(barrier_num, barriers.data());
}
void ExecuteCommandList(D3d12CommandList** command_lists, const uint32_t command_list_num, ID3D12CommandQueue* command_queue, CommandList* pool) {
  for (uint32_t i = 0; i < command_list_num; i++) {
    command_lists[i]->Close();
  }
  command_queue->ExecuteCommandLists(command_list_num, reinterpret_cast<ID3D12CommandList**>(command_lists));
  pool->ReturnCommandList(command_lists);
}
constexpr D3D12_RESOURCE_DIMENSION Dimension(const BufferDimensionType type) {
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
}
constexpr D3D12_RESOURCE_FLAGS ResourceFlags(const BufferStateFlags state_flags) {
  D3D12_RESOURCE_FLAGS flags{};
  if (state_flags & kBufferStateFlagUav) flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (state_flags & kBufferStateFlagRtv) flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if ((state_flags & kBufferStateFlagDsvWrite) || (state_flags & kBufferStateFlagDsvRead)) flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  if ((flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) && !(state_flags & kBufferStateFlagCbv) && !(state_flags & kBufferStateFlagSrv)) flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  return flags;
}
constexpr D3D12_RESOURCE_DESC GetD3d12ResourceDesc(const BufferCreationDesc& desc) {
  D3D12_RESOURCE_DESC resource_desc{};
  resource_desc.Dimension = Dimension(desc.dimension_type);
  resource_desc.Alignment = 0;
  resource_desc.Width = desc.width;
  resource_desc.Height = desc.height;
  resource_desc.DepthOrArraySize = static_cast<uint16_t>(desc.depth);
  resource_desc.MipLevels = 1; // TODO?
  resource_desc.Format = GetDxgiFormat(desc.format);
  resource_desc.SampleDesc = {1, 0};
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = ResourceFlags(desc.state_flags);
  return resource_desc;
}
std::tuple<std::pmr::unordered_map<BufferId, uint32_t>, std::pmr::unordered_map<BufferId, uint32_t>> GetPhysicalBufferSizes(const BufferCreationDescList& buffer_creation_descs, D3d12Device* const device, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_size_in_byte{memory_resource};
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_alignment{memory_resource};
  for (auto& [id, desc] : buffer_creation_descs) {
    auto resource_desc = GetD3d12ResourceDesc(desc);
    auto alloc_info = device->GetResourceAllocationInfo(0, 1, &resource_desc);
    physical_buffer_size_in_byte.insert({id, alloc_info.SizeInBytes});
    physical_buffer_alignment.insert({id, alloc_info.Alignment});
  }
  return {physical_buffer_size_in_byte, physical_buffer_alignment};
}
constexpr D3D12_CLEAR_VALUE GetClearValue(const BufferCreationDesc& desc) {
  D3D12_CLEAR_VALUE clear_value{};
  clear_value.Format = GetDxgiFormat(desc.format);
  if (std::holds_alternative<ClearValueDepthStencil>(desc.clear_value)) {
    auto& depth_stencil = std::get<ClearValueDepthStencil>(desc.clear_value);
    clear_value.DepthStencil.Depth = depth_stencil.depth;
    clear_value.DepthStencil.Stencil = depth_stencil.stencil;
  } else {
    auto& color = std::get<std::array<float, 4>>(desc.clear_value);
    clear_value.Color[0] = color[0];
    clear_value.Color[1] = color[1];
    clear_value.Color[2] = color[2];
    clear_value.Color[3] = color[3];
  }
  return clear_value;
}
constexpr bool IsOptimizedClearValueNeeded(const BufferCreationDesc& desc) {
  if (desc.dimension_type == BufferDimensionType::kBuffer) return false;
  if (desc.state_flags & kBufferStateFlagRtv) return true;
  if (desc.state_flags & kBufferStateFlagDsvWrite) return true;
  if (desc.state_flags & kBufferStateFlagDsvRead) return true;
  return false;
}
std::pair<D3D12MA::Allocation*, ID3D12Resource*> CreatePhysicalBufferOnHeap(const D3D12_HEAP_TYPE heap_type, const BufferCreationDesc& desc, D3D12MA::Allocator* const allocator) {
  D3D12MA::ALLOCATION_DESC allocation_desc{};
  allocation_desc.HeapType = heap_type;
  auto resource_desc = GetD3d12ResourceDesc(desc);
  auto clear_value = GetClearValue(desc);
  D3D12MA::Allocation* allocation = nullptr;
  ID3D12Resource* resource = nullptr;
  auto hr = allocator->CreateResource(&allocation_desc, &resource_desc, ConvertToD3d12ResourceState(desc.initial_state_flag), IsOptimizedClearValueNeeded(desc) ? &clear_value : nullptr, &allocation, IID_PPV_ARGS(&resource));
  if (SUCCEEDED(hr)) {
    return {allocation, resource};
  }
  logerror("CreateResource failed. {} {}", hr, heap_type);
  return {nullptr, nullptr};
}
using PhysicalBufferList = std::pmr::unordered_map<BufferId, ID3D12Resource*>;
using PhysicalAllocationList = std::pmr::unordered_map<BufferId, D3D12MA::Allocation*>;
std::pair<PhysicalAllocationList, PhysicalBufferList> CreatePhysicalBuffers(const BufferCreationDescList& buffer_creation_descs, [[maybe_unused]]const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_size_in_byte, [[maybe_unused]]const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_alignment, [[maybe_unused]]const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_address_offset, [[maybe_unused]]PhysicalBufferList&& physical_buffer, D3D12MA::Allocator* const allocator, std::pmr::memory_resource* memory_resource) {
  // TODO resource aliasing (available in library?)
  PhysicalAllocationList physical_allocation{memory_resource};
  for (auto& [id, desc] : buffer_creation_descs) {
    if (physical_buffer.contains(id)) continue;
    auto pair = CreatePhysicalBufferOnHeap(D3D12_HEAP_TYPE_DEFAULT, desc, allocator);
    physical_allocation.insert({id, pair.first});
    physical_buffer.insert({id, pair.second});
  }
  return {physical_allocation, std::move(physical_buffer)};
}
D3D12MA::Allocator* CreateMemoryHeapAllocator(D3d12Device* const device, DxgiAdapter* const adapter) {
  D3D12MA::Allocator* allocator = nullptr;
  D3D12MA::ALLOCATOR_DESC desc = {};
  desc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;
  desc.pDevice = device;
  desc.pAdapter = adapter;
  D3D12MA::CreateAllocator(&desc, &allocator);
  return allocator;
}
constexpr D3D12_SHADER_RESOURCE_VIEW_DESC GetD3d12ShaderResourceViewDesc(const BufferConfig& desc, ID3D12Resource* resource) {
  // not checked yet.
  D3D12_SHADER_RESOURCE_VIEW_DESC view_desc{};
  view_desc.Format = GetDxgiFormat(desc.format);
  view_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  switch (desc.dimension_type) {
    case BufferDimensionType::kBuffer: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
      view_desc.Buffer.FirstElement = 0;
      view_desc.Buffer.NumElements = 1;
      view_desc.Buffer.StructureByteStride = 8;
      view_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
      break;
    }
    case BufferDimensionType::k1d: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
      view_desc.Texture1D.MostDetailedMip = 0;
      view_desc.Texture1D.MipLevels = 1;
      view_desc.Texture1D.ResourceMinLODClamp = 0.0f;
      break;
    }
    case BufferDimensionType::k1dArray: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
      view_desc.Texture1DArray.MostDetailedMip = 0;
      view_desc.Texture1DArray.MipLevels = 1;
      view_desc.Texture1DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture1DArray.ArraySize = desc.buffer_num_to_render;
      view_desc.Texture1DArray.ResourceMinLODClamp = 0.0f;
      break;
    }
    case BufferDimensionType::k2d: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      view_desc.Texture2D.MostDetailedMip = 0;
      view_desc.Texture2D.MipLevels = 1;
      view_desc.Texture2D.PlaneSlice = desc.index_to_render;
      view_desc.Texture2D.ResourceMinLODClamp = 0.0f;
      break;
    }
    case BufferDimensionType::k2dArray: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
      view_desc.Texture2DArray.MostDetailedMip = 0;
      view_desc.Texture2DArray.MipLevels = 1;
      view_desc.Texture2DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture2DArray.ArraySize = desc.buffer_num_to_render;
      view_desc.Texture2DArray.PlaneSlice = 0; // not sure w/FirstArraySlice
      view_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
      break;
    }
    case BufferDimensionType::k3d: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      view_desc.Texture3D.MostDetailedMip = 0;
      view_desc.Texture3D.MipLevels = 1;
      view_desc.Texture3D.ResourceMinLODClamp = 0.0f;
      break;
    }
    case BufferDimensionType::kCube: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      view_desc.TextureCube.MostDetailedMip = 0;
      view_desc.TextureCube.MipLevels = 1;
      view_desc.TextureCube.ResourceMinLODClamp = 0.0f;
      break;
    }
    case BufferDimensionType::kCubeArray: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
      view_desc.TextureCubeArray.MostDetailedMip = 0;
      view_desc.TextureCubeArray.MipLevels = 1;
      view_desc.TextureCubeArray.First2DArrayFace = desc.index_to_render;
      view_desc.TextureCubeArray.NumCubes = desc.buffer_num_to_render;
      view_desc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
      break;
    }
    case BufferDimensionType::kAS: {
      view_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
      view_desc.RaytracingAccelerationStructure.Location = resource->GetGPUVirtualAddress();
      break;
    }
  }
  return view_desc;
}
constexpr D3D12_UNORDERED_ACCESS_VIEW_DESC GetD3d12UnorderedAccessViewDesc(const BufferConfig& desc) {
  D3D12_UNORDERED_ACCESS_VIEW_DESC view_desc{};
  view_desc.Format = GetDxgiFormat(desc.format);
  switch (desc.dimension_type) {
    case BufferDimensionType::kBuffer: {
      view_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
      view_desc.Buffer.FirstElement = 0;
      view_desc.Buffer.NumElements = 1;
      view_desc.Buffer.StructureByteStride = 8;
      view_desc.Buffer.CounterOffsetInBytes = 0;
      view_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
      break;
    }
    case BufferDimensionType::k1d: {
      view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D;
      view_desc.Texture1D.MipSlice = 0;
      break;
    }
    case BufferDimensionType::k1dArray: {
      view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY;
      view_desc.Texture1DArray.MipSlice = 0;
      view_desc.Texture1DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture1DArray.ArraySize = desc.buffer_num_to_render;
      break;
    }
    case BufferDimensionType::k2d: {
      view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
      view_desc.Texture2D.MipSlice = 0;
      view_desc.Texture2D.PlaneSlice = desc.index_to_render;
      break;
    }
    case BufferDimensionType::k2dArray: {
      view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
      view_desc.Texture2DArray.MipSlice = 0;
      view_desc.Texture2DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture2DArray.ArraySize = desc.buffer_num_to_render;
      view_desc.Texture2DArray.PlaneSlice = 0; // not sure w/FirstArraySlice
      break;
    }
    case BufferDimensionType::k3d: {
      view_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
      view_desc.Texture3D.MipSlice = 0;
      view_desc.Texture3D.FirstWSlice = desc.index_to_render;
      view_desc.Texture3D.WSize = desc.buffer_num_to_render;
      break;
    }
    case BufferDimensionType::kCube:
    case BufferDimensionType::kCubeArray:
    case BufferDimensionType::kAS: {
      // not supported.
      break;
    }
  }
  return view_desc;
}
constexpr D3D12_RENDER_TARGET_VIEW_DESC GetD3d12RenderTargetViewDesc(const BufferConfig& desc) {
  D3D12_RENDER_TARGET_VIEW_DESC view_desc{};
  view_desc.Format = GetDxgiFormat(desc.format);
  switch (desc.dimension_type) {
    case BufferDimensionType::kBuffer: {
      view_desc.ViewDimension = D3D12_RTV_DIMENSION_BUFFER;
      view_desc.Buffer.FirstElement = 0;
      view_desc.Buffer.NumElements = 1;
      break;
    }
    case BufferDimensionType::k1d: {
      view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D;
      view_desc.Texture1D.MipSlice = 0;
      break;
    }
    case BufferDimensionType::k1dArray: {
      view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
      view_desc.Texture1DArray.MipSlice = 0;
      view_desc.Texture1DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture1DArray.ArraySize = desc.buffer_num_to_render;
      break;
    }
    case BufferDimensionType::k2d: {
      view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
      view_desc.Texture2D.MipSlice = 0;
      view_desc.Texture2D.PlaneSlice = desc.index_to_render;
      break;
    }
    case BufferDimensionType::k2dArray: {
      view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
      view_desc.Texture2DArray.MipSlice = 0;
      view_desc.Texture2DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture2DArray.ArraySize = desc.buffer_num_to_render;
      view_desc.Texture2DArray.PlaneSlice = 0; // not sure w/FirstArraySlice
      break;
    }
    case BufferDimensionType::k3d: {
      view_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
      view_desc.Texture3D.MipSlice = 0;
      view_desc.Texture3D.FirstWSlice = desc.index_to_render;
      view_desc.Texture3D.WSize = desc.buffer_num_to_render;
      break;
    }
    case BufferDimensionType::kCube:
    case BufferDimensionType::kCubeArray:
    case BufferDimensionType::kAS: {
      // not supported.
      break;
    }
  }
  return view_desc;
}
constexpr D3D12_DSV_FLAGS GetD3d12DepthStencilFlag(const DepthStencilFlag depth_stencil_flag) {
  D3D12_DSV_FLAGS flag{};
  if (depth_stencil_flag & kDepthStencilFlagDepth)   flag |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
  if (depth_stencil_flag & kDepthStencilFlagStencil) flag |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
  return flag;
}
constexpr D3D12_DEPTH_STENCIL_VIEW_DESC GetD3d12DepthStencilViewDesc(const BufferConfig& desc) {
  D3D12_DEPTH_STENCIL_VIEW_DESC view_desc{};
  view_desc.Format = GetDxgiFormat(desc.format);
  view_desc.Flags = GetD3d12DepthStencilFlag(desc.depth_stencil_flag);
  switch (desc.dimension_type) {
    case BufferDimensionType::k1d: {
      view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D;
      view_desc.Texture1D.MipSlice = 0;
      break;
    }
    case BufferDimensionType::k1dArray: {
      view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
      view_desc.Texture1DArray.MipSlice = 0;
      view_desc.Texture1DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture1DArray.ArraySize = desc.buffer_num_to_render;
      break;
    }
    case BufferDimensionType::k2d: {
      view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
      view_desc.Texture2D.MipSlice = 0;
      break;
    }
    case BufferDimensionType::k2dArray: {
      view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
      view_desc.Texture2DArray.MipSlice = 0;
      view_desc.Texture2DArray.FirstArraySlice = desc.index_to_render;
      view_desc.Texture2DArray.ArraySize = desc.buffer_num_to_render;
      break;
    }
    case BufferDimensionType::kBuffer:
    case BufferDimensionType::k3d:
    case BufferDimensionType::kCube:
    case BufferDimensionType::kCubeArray:
    case BufferDimensionType::kAS: {
      // not supported.
      break;
    }
  }
  return view_desc;
}
struct BufferInfoSet {
  BufferInfoSet()
      : physical_buffer_allocator(nullptr) {}
  BufferInfoSet(D3D12MA::Allocator* const allocator, std::pmr::memory_resource* memory_resource)
      : physical_buffer_allocator(allocator)
      , physical_buffer(memory_resource)
      , physical_allocation(memory_resource)
      , gpu_descriptor_handles(memory_resource)
      , cpu_descriptor_handles(memory_resource)
      , pass_resources(memory_resource) {}
  D3D12MA::Allocator* physical_buffer_allocator;
  PhysicalBufferList physical_buffer;
  PhysicalAllocationList physical_allocation;
  std::pmr::unordered_map<StrId, D3D12_GPU_DESCRIPTOR_HANDLE> gpu_descriptor_handles; // cbv, srv, uav
  std::pmr::unordered_map<StrId, std::pmr::vector<D3D12_CPU_DESCRIPTOR_HANDLE>> cpu_descriptor_handles; // uav, dsv, rtv
  std::pmr::unordered_map<StrId, std::pmr::vector<ID3D12Resource*>> pass_resources; // uav, copy_src, copy_dst
  BufferStateList current_buffer_state_list;
};
using CpuHandleListPerBuffer = std::pmr::unordered_map<BufferId, std::pmr::unordered_map<BufferStateType, D3D12_CPU_DESCRIPTOR_HANDLE>>;
BufferInfoSet CreateBufferInfoSet(D3d12Device* const device, DxgiAdapter* const adapter,
                                  const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list,
                                  const BufferSize2d& mainbuffer_size, const BufferSize2d& swapchain_size,
                                  PhysicalBufferList&& external_buffers, CpuHandleListPerBuffer&& external_cpu_handles,
                                  DescriptorHeap* const descriptor_heap_buffers, DescriptorHeap* const descriptor_heap_rtv, DescriptorHeap* const descriptor_heap_dsv,
                                  ShaderVisibleDescriptorHeap* const shader_visible_descriptor_heap,
                                  std::pmr::memory_resource* memory_resource) {
  BufferInfoSet set(CreateMemoryHeapAllocator(device, adapter), memory_resource);
  // prepare physical buffers
  auto buffer_creation_descs = ConfigureBufferCreationDescs(render_pass_id_map, render_pass_order, buffer_id_list, mainbuffer_size, swapchain_size, memory_resource);
  auto [physical_buffer_size_in_byte, physical_buffer_alignment] = GetPhysicalBufferSizes(buffer_creation_descs, device, memory_resource);
  {
    // TODO aliasing barrier
    auto [physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass] = CalculatePhysicalBufferLiftime(render_pass_order, buffer_id_list, memory_resource);
    auto physical_buffer_address_offset = GetPhysicalBufferAddressOffset(render_pass_order, physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass, physical_buffer_size_in_byte, physical_buffer_alignment, memory_resource);
    std::tie(set.physical_allocation, set.physical_buffer) = CreatePhysicalBuffers(buffer_creation_descs, physical_buffer_size_in_byte, physical_buffer_alignment, physical_buffer_address_offset, std::move(external_buffers), set.physical_buffer_allocator, memory_resource);
  }
  // prepare pass buffer handles and resources
  auto& physical_buffer = set.physical_buffer;
  auto& gpu_descriptor_handles = set.gpu_descriptor_handles;
  auto& cpu_descriptor_handles = set.cpu_descriptor_handles;
  auto& pass_resources = set.pass_resources;
  auto&& cpu_descriptor_handles_per_buffer = std::move(external_cpu_handles);
  for (auto& [pass_name, buffer_ids] : buffer_id_list) {
    auto& pass = render_pass_id_map.at(pass_name);
    std::pmr::vector<D3D12_CPU_DESCRIPTOR_HANDLE> handles_to_copy_to_gpu{memory_resource};
    for (uint32_t i = 0; i < buffer_ids.size(); i++) {
      auto& buffer_id = buffer_ids[i];
      if (!cpu_descriptor_handles_per_buffer.contains(buffer_id)) {
        cpu_descriptor_handles_per_buffer.insert({buffer_id, std::pmr::unordered_map<BufferStateType, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource}});
      }
      auto state_type = pass.buffer_list[i].state_type;
      auto create_handle = !cpu_descriptor_handles_per_buffer.at(buffer_id).contains(state_type);
      if (create_handle) {
        auto descriptor_heap = descriptor_heap_buffers;
        if (state_type == BufferStateType::kRtv) {
          descriptor_heap = descriptor_heap_rtv;
        } else if (state_type == BufferStateType::kDsv) {
          descriptor_heap = descriptor_heap_dsv;
        }
        auto handle = descriptor_heap->RetainHandle();
        cpu_descriptor_handles_per_buffer.at(buffer_id).insert({state_type, handle});
      }
      auto& cpu_handle = cpu_descriptor_handles_per_buffer.at(buffer_id).at(state_type);
      auto& resource = physical_buffer.at(buffer_id);
      auto& buffer_config = pass.buffer_list[i];
      bool push_back_cpu_handle = false;
      bool push_back_gpu_handle = false;
      bool push_back_resource = false;
      switch (state_type) {
        case BufferStateType::kCbv: {
          if (create_handle) {
            D3D12_CONSTANT_BUFFER_VIEW_DESC desc{resource->GetGPUVirtualAddress(), physical_buffer_size_in_byte.at(buffer_id)};
            device->CreateConstantBufferView(&desc, cpu_handle);
          }
          push_back_gpu_handle = true;
          break;
        }
        case BufferStateType::kSrvPsOnly:
        case BufferStateType::kSrvNonPs:
        case BufferStateType::kSrvAll: {
          if (create_handle) {
            auto desc = GetD3d12ShaderResourceViewDesc(buffer_config, resource);
            device->CreateShaderResourceView(resource, &desc, cpu_handle);
          }
          push_back_gpu_handle = true;
          break;
        }
        case BufferStateType::kUav: {
          if (create_handle) {
            auto desc = GetD3d12UnorderedAccessViewDesc(buffer_config);
            device->CreateUnorderedAccessView(resource, nullptr, &desc, cpu_handle);
          }
          push_back_cpu_handle = true;
          push_back_gpu_handle = true;
          push_back_resource = true;
          break;
        }
        case BufferStateType::kRtv: {
          if (create_handle) {
            auto desc = GetD3d12RenderTargetViewDesc(buffer_config);
            device->CreateRenderTargetView(resource, &desc, cpu_handle);
          }
          push_back_cpu_handle = true;
          break;
        }
        case BufferStateType::kDsv: {
          if (create_handle) {
            auto desc = GetD3d12DepthStencilViewDesc(buffer_config);
            device->CreateDepthStencilView(resource, &desc, cpu_handle);
          }
          push_back_cpu_handle = true;
          break;
        }
        case BufferStateType::kCopySrc:
        case BufferStateType::kCopyDst: {
          push_back_resource = true;
          break;
        }
        case BufferStateType::kPresent: {
          break;
        }
      }
      if (push_back_cpu_handle) {
        cpu_descriptor_handles[pass_name].push_back(cpu_handle);
      }
      if (push_back_gpu_handle) {
        handles_to_copy_to_gpu.push_back(cpu_handle);
      }
      if (push_back_resource) {
        pass_resources[pass_name].push_back(resource);
      }
    }
    if (!handles_to_copy_to_gpu.empty()) {
      auto gpu_handle = shader_visible_descriptor_heap->CopyToBufferDescriptorHeap(handles_to_copy_to_gpu.data(), static_cast<uint32_t>(handles_to_copy_to_gpu.size()), memory_resource);
      gpu_descriptor_handles.insert({pass_name, gpu_handle});
    }
  }
  // barrier configuration
  set.current_buffer_state_list = CreateBufferCreationStateList(buffer_creation_descs, memory_resource);
  return set;
}
void UpdateExternalBufferPointers(const PhysicalBufferList& external_buffers, const CpuHandleListPerBuffer& external_handles, const RenderPassIdMap& render_pass_id_map, const RenderPassOrder& render_pass_order, const BufferIdList& buffer_id_list, BufferInfoSet* const dst) {
  for (auto& [buffer_id , resource] : external_buffers) {
    dst->physical_buffer.insert_or_assign(buffer_id, resource);
  }
  for (auto& pass_name: render_pass_order) {
    auto& pass = render_pass_id_map.at(pass_name);
    auto& buffers = buffer_id_list.at(pass_name);
    for (uint32_t buffer_index = 0; buffer_index < buffers.size(); buffer_index++) {
      auto& buffer_id = buffers[buffer_index];
      if (external_handles.contains(buffer_id)) {
        auto& buffer_config = pass.buffer_list[buffer_index];
        if (buffer_config.state_type == BufferStateType::kRtv && external_handles.at(buffer_id).contains(BufferStateType::kRtv)) {
          uint32_t buffer_index_in_cpu_descritors = 0;
          for (uint32_t check_buffer_index = 0; check_buffer_index < buffer_index; check_buffer_index++) {
            if (auto& state_type = pass.buffer_list[check_buffer_index].state_type; state_type == BufferStateType::kUav ||  state_type == BufferStateType::kRtv ||  state_type == BufferStateType::kDsv) {
              buffer_index_in_cpu_descritors++;
            }
          }
          dst->cpu_descriptor_handles.at(pass_name)[buffer_index_in_cpu_descritors] = external_handles.at(buffer_id).at(BufferStateType::kRtv);
        } else {
          ASSERT(false);
        }
      }
    }
  }
}
void RetainCommandList(const CommandQueueType pass_queue_type, CommandAllocator* command_allocator, std::pmr::unordered_map<CommandQueueType, D3d12CommandList**>* command_lists, CommandList* command_list_pool, ShaderVisibleDescriptorHeap* shader_visible_descriptor_heap, std::vector<std::vector<ID3D12CommandAllocator**>>* allocators) {
  if (command_lists->contains(pass_queue_type)) return;
  auto command_allocators = command_allocator->RetainCommandAllocator(pass_queue_type, 1);
  allocators->back().push_back(std::move(command_allocators));
  command_lists->insert({pass_queue_type, command_list_pool->RetainCommandList(pass_queue_type, 1, command_allocators)});
  shader_visible_descriptor_heap->SetDescriptorHeapsToCommandList((*command_lists)[pass_queue_type][0]);
}
PassBarrierInfoSet MergeBarriers(PassBarrierInfoSet&& a, PassBarrierInfoSet&& b) {
  for (auto&& pair : b.barrier_before_pass) {
    if (a.barrier_before_pass.contains(pair.first)) {
      a.barrier_before_pass.at(pair.first).insert(a.barrier_before_pass.at(pair.first).end(), std::make_move_iterator(pair.second.begin()), std::make_move_iterator(pair.second.end()));
    } else {
      a.barrier_before_pass.insert(std::move(pair));
    }
  }
  for (auto&& pair : b.barrier_after_pass) {
    if (a.barrier_after_pass.contains(pair.first)) {
      a.barrier_after_pass.at(pair.first).insert(a.barrier_after_pass.at(pair.first).end(), std::make_move_iterator(pair.second.begin()), std::make_move_iterator(pair.second.end()));
    } else {
      a.barrier_after_pass.insert(std::move(pair));
    }
  }
  return a;
}
}
TEST_CASE("d3d12/render") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const uint32_t buffer_size_in_bytes = 32 * 1024;
  std::byte buffer[buffer_size_in_bytes]{};
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  const uint32_t buffer_num = 2;
  const uint32_t swapchain_buffer_num = buffer_num + 1;
  const BufferSize2d swapchain_size{1600, 900};
  const auto mainbuffer_size = swapchain_size;
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  CommandQueue command_queue;
  CHECK(command_queue.Init(device.Get()));
  illuminate::gfx::win32::Window window;
  CHECK(window.Init("swapchain test", swapchain_size.width, swapchain_size.height));
  Swapchain swapchain;
  CHECK(swapchain.Init(dxgi_core.GetFactory(), command_queue.Get(CommandQueueType::kGraphics), device.Get(), window.GetHwnd(), swapchain_buffer_num, buffer_num));
  ShaderVisibleDescriptorHeap shader_visible_descriptor_heap;
  CHECK(shader_visible_descriptor_heap.Init(device.Get()));
  ShaderCompiler shader_compiler;
  CHECK(shader_compiler.Init(device.Get()));
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.Get()));
  CommandList command_list_pool;
  CHECK(command_list_pool.Init(device.Get()));
  std::vector<std::vector<ID3D12CommandAllocator**>> allocators(buffer_num);
  DescriptorHeap descriptor_heap_buffers, descriptor_heap_rtv, descriptor_heap_dsv;
  const uint32_t descriptor_handle_num = 8;
  CHECK(descriptor_heap_buffers.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_handle_num));
  CHECK(descriptor_heap_rtv.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, descriptor_handle_num));
  CHECK(descriptor_heap_dsv.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, descriptor_handle_num));
  RenderPassList render_pass_list{memory_resource.get()};
  using RenderFunction = std::function<void(D3d12CommandList* const, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, ID3D12Resource** resource)>;
  std::pmr::unordered_map<StrId, RenderFunction> render_functions{memory_resource.get()};
  const uint32_t buffer_tmp_size_in_bytes = 32 * 1024;
  std::byte buffer_tmp[buffer_tmp_size_in_bytes]{};
  std::function<void()> term_func;
  SUBCASE("clear swapchain rtv@graphics queue") {
    render_pass_list.push_back(RenderPass(
        StrId("mainpass"),
        {
          {
            BufferConfig(StrId("swapchain"), BufferStateType::kRtv),
          },
          memory_resource.get()
        }
                                          ));
    render_functions.insert({StrId("mainpass"), [](D3d12CommandList* const command_list, [[maybe_unused]]const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, [[maybe_unused]]ID3D12Resource** resource){
      const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
      command_list->ClearRenderTargetView(cpu_handle[0], clear_color, 0, nullptr);
    }});
  }
  SUBCASE("clear swapchain uav@compute queue") {
    // swapchain can only be used as rtv (no uav, dsv, copy_dst, etc.)
    render_pass_list.push_back(RenderPass(
        StrId("mainpass"),
        {
          {
            BufferConfig(StrId("mainbuffer"), BufferStateType::kUav),
          },
          memory_resource.get()
        }
                                          ).CommandQueueTypeCompute());
    render_pass_list.push_back(RenderPass(
        StrId("copy"),
        {
          {
            BufferConfig(StrId("mainbuffer"), BufferStateType::kSrvPsOnly),
            BufferConfig(StrId("swapchain"), BufferStateType::kRtv),
          },
          memory_resource.get()
        }
                                          ));
    ID3D12RootSignature* copy_root_signature = nullptr;
    ID3D12PipelineState* copy_pipeline_state = nullptr;
    {
      auto memory_resource_tmp = std::make_shared<PmrLinearAllocator>(buffer_tmp, buffer_tmp_size_in_bytes);
      auto shader_result_vs = shader_compiler.Compile(L"shader/test/fullscreen-triangle.vs.hlsl", ShaderType::kVs, memory_resource_tmp.get());
      CHECK(shader_result_vs);
      auto shader_result_ps = shader_compiler.Compile(L"shader/test/copysrv.ps.hlsl", ShaderType::kPs, memory_resource_tmp.get());
      CHECK(shader_result_ps);
      auto root_signature_blob = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_ps, DXC_OUT_ROOT_SIGNATURE);
      CHECK(root_signature_blob);
      auto hr = device.Get()->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&copy_root_signature));
      CHECK(SUCCEEDED(hr));
      CHECK(copy_root_signature);
      auto shader_object_vs = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_vs, DXC_OUT_OBJECT);
      CHECK(shader_object_vs);
      auto shader_object_ps = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result_ps, DXC_OUT_OBJECT);
      CHECK(shader_object_ps);
      struct {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature;
        CD3DX12_PIPELINE_STATE_STREAM_VS vs;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS render_target_formats;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY topology;
      } desc_local {
        copy_root_signature,
        D3D12_SHADER_BYTECODE{shader_object_vs->GetBufferPointer(), shader_object_vs->GetBufferSize()},
        D3D12_SHADER_BYTECODE{shader_object_ps->GetBufferPointer(), shader_object_ps->GetBufferSize()},
        {{{swapchain.GetDxgiFormat()}, 1}},
        D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
      };
      D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(desc_local), &desc_local};
      hr = device.Get()->CreatePipelineState(&desc, IID_PPV_ARGS(&copy_pipeline_state));
      CHECK(SUCCEEDED(hr));
      CHECK(copy_pipeline_state);
      shader_object_ps->Release();
      shader_object_vs->Release();
      shader_result_ps->Release();
      shader_result_vs->Release();
    }
    render_functions.insert({StrId("mainpass"), [](D3d12CommandList* const command_list, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, ID3D12Resource** resource){
      const UINT clear_color[4]{255,255,0,255};
      command_list->ClearUnorderedAccessViewUint(gpu_handle, cpu_handle[0], resource[0], clear_color, 0, nullptr);
    }});
    render_functions.insert({StrId("copy"), [copy_root_signature, copy_pipeline_state](D3d12CommandList* const command_list, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, [[maybe_unused]]ID3D12Resource** resource){
      command_list->SetGraphicsRootSignature(copy_root_signature);
      command_list->SetPipelineState(copy_pipeline_state);
      command_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      command_list->SetGraphicsRootDescriptorTable(0, gpu_handle);
      const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
      command_list->OMSetRenderTargets(1, cpu_handle, true, nullptr);
      command_list->DrawInstanced(3, 1, 0, 0);
    }});
    term_func = [copy_root_signature, copy_pipeline_state]() {
      copy_root_signature->Release();
      copy_pipeline_state->Release();
    };
  }
#if 0
  // TODO
  SUBCASE("fill uav with shader@compute queue and copy to swapchain buffer") {
  }
  SUBCASE("clear + draw triangle to swapchain w/dsv") {
  }
  SUBCASE("clear + draw moving triangle to swapchain w/dsv") {
  }
  SUBCASE("clear + draw moving triangle to rtv w/dsv, copy to uav@compute queue") {
  }
  SUBCASE("transfer texture from cpu and use@graphics queue") {
  }
  SUBCASE("imgui") {
  }
#endif
  auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
  auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
  MandatoryOutputBufferNameList named_buffer_list{memory_resource.get()};
  named_buffer_list.insert({StrId("swapchain")});
  auto named_buffers = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, named_buffer_list, memory_resource.get());
  {
    // cull unused render pass
    auto memory_resource_tmp = std::make_shared<PmrLinearAllocator>(buffer_tmp, buffer_tmp_size_in_bytes);
    auto adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource_tmp.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(adjacency_graph, memory_resource_tmp.get());
    auto used_render_pass_list = GetBufferProducerPassList(adjacency_graph, CreateValueSetFromMap(named_buffers, memory_resource_tmp.get()), memory_resource.get());
    used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
    render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
    buffer_id_list = RemoveUnusedBuffers(render_pass_order, std::move(buffer_id_list));
    memory_resource_tmp->Reset();
  }
  PassSignalInfo pass_signal_info;
  {
    // queue signal, wait info
    auto memory_resource_tmp = std::make_shared<PmrLinearAllocator>(buffer_tmp, buffer_tmp_size_in_bytes);
    auto adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource_tmp.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(adjacency_graph, memory_resource_tmp.get());
    auto [async_compute_batching, render_pass_unprocessed] = ConfigureAsyncComputeBatching(render_pass_id_map, std::move(render_pass_order), {}, {}, memory_resource.get());
    auto pass_signal_info_resource = ConfigureBufferResourceDependency(render_pass_id_map, async_compute_batching, consumer_producer_render_pass_map, memory_resource.get());
    auto pass_signal_info_batch = ConvertBatchToSignalInfo(async_compute_batching, render_pass_id_map, memory_resource.get());
    pass_signal_info = MergePassSignalInfo(std::move(pass_signal_info_resource), std::move(pass_signal_info_batch));
    render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(async_compute_batching), memory_resource.get());
    memory_resource_tmp->Reset();
  }
  BufferInfoSet buffers;
  {
    auto& swapchain_buffer_id = named_buffers.at(StrId("swapchain"));
    PhysicalBufferList external_buffers{memory_resource.get()};
    external_buffers.insert({swapchain_buffer_id, swapchain.GetResource()});
    CpuHandleListPerBuffer external_handles{memory_resource.get()};
    external_handles.insert({swapchain_buffer_id, std::pmr::unordered_map<BufferStateType, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource.get()}});
    external_handles.at(swapchain_buffer_id).insert({BufferStateType::kRtv, swapchain.GetRtvHandle()});
    buffers = CreateBufferInfoSet(device.Get(), dxgi_core.GetAdapter(),
                                  render_pass_id_map, render_pass_order, buffer_id_list,
                                  mainbuffer_size, swapchain_size,
                                  std::move(external_buffers), std::move(external_handles),
                                  &descriptor_heap_buffers, &descriptor_heap_rtv, &descriptor_heap_dsv,
                                  &shader_visible_descriptor_heap,
                                  memory_resource.get()); // TODO refactor
  }
  const uint32_t kFrameNumToTest = 10;
  std::pmr::unordered_map<CommandQueueType, uint64_t> pass_signal_val{memory_resource.get()};
  std::pmr::vector<std::unordered_map<CommandQueueType, uint64_t>> frame_wait_signal{memory_resource.get()};
  frame_wait_signal.resize(buffer_num);
  PassBarrierInfoSet barriers;
  const uint32_t buffer_double_buffered_size_in_bytes = 4 * 1024;
  std::byte buffer_double_buffered[buffer_double_buffered_size_in_bytes * 2]{};
  auto memory_resource_double_buffered = std::make_shared<PmrDoubleBufferedAllocator>(buffer_double_buffered, &buffer_double_buffered[buffer_double_buffered_size_in_bytes], buffer_double_buffered_size_in_bytes);
  auto memory_resource_tmp = std::make_shared<PmrLinearAllocator>(buffer_tmp, buffer_tmp_size_in_bytes);
  size_t memory_resource_tmp_max_used_bytes = 0, memory_resource_double_buffered_max_used_bytes = 0;
  for (uint32_t frame_no = 0; frame_no < kFrameNumToTest; frame_no++) {
    auto frame_index = frame_no % buffer_num;
    command_queue.WaitOnCpu(std::move(frame_wait_signal[frame_index]));
    BufferStateList buffer_state_after_render_pass_list{memory_resource_tmp.get()};
    {
      swapchain.UpdateBackBufferIndex();
      auto& swapchain_buffer_id = named_buffers.at(StrId("swapchain"));
      PhysicalBufferList external_buffers{memory_resource_tmp.get()};
      external_buffers.insert({swapchain_buffer_id, swapchain.GetResource()});
      CpuHandleListPerBuffer external_handles{memory_resource_tmp.get()};
      external_handles.insert({swapchain_buffer_id, std::pmr::unordered_map<BufferStateType, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource_tmp.get()}});
      external_handles.at(swapchain_buffer_id).insert({BufferStateType::kRtv, swapchain.GetRtvHandle()});
      UpdateExternalBufferPointers(external_buffers, external_handles, render_pass_id_map, render_pass_order, buffer_id_list, &buffers);
      buffers.current_buffer_state_list.insert_or_assign(swapchain_buffer_id, kBufferStateFlagPresent);
      buffer_state_after_render_pass_list.insert({swapchain_buffer_id, kBufferStateFlagPresent});
    }
    auto buffer_state_change_list = GatherBufferStateChangeInfo(render_pass_id_map, render_pass_order, buffer_id_list, buffers.current_buffer_state_list, buffer_state_after_render_pass_list, memory_resource_tmp.get());
    auto inter_pass_distance_map = CreateInterPassDistanceMap(render_pass_id_map, render_pass_order, pass_signal_info, memory_resource_tmp.get());
    barriers = MergeBarriers(std::move(barriers), ConfigureBarrier(render_pass_id_map, buffer_state_change_list, inter_pass_distance_map, memory_resource_tmp.get()));
    auto [current_frame_barriers, next_frame_barriers] = ConfigureBarrierForNextFrame(render_pass_id_map, render_pass_order, buffers.current_buffer_state_list, buffer_state_after_render_pass_list, inter_pass_distance_map, buffer_state_change_list, memory_resource_double_buffered.get());
    barriers = MergeBarriers(std::move(barriers), std::move(current_frame_barriers));
    {
      for (auto a : allocators.front()) {
        command_allocator.ReturnCommandAllocator(a);
      }
      allocators.front().clear();
      std::rotate(allocators.begin(), allocators.begin() + 1, allocators.end());
    }
    std::pmr::unordered_map<StrId, std::pmr::unordered_map<CommandQueueType, uint64_t>> waiting_pass{memory_resource_tmp.get()};
    std::pmr::unordered_map<CommandQueueType, D3d12CommandList**> command_lists{memory_resource_tmp.get()};
    for (auto& pass_name : render_pass_order) {
      auto pass_queue_type = render_pass_id_map.at(pass_name).command_queue_type;
      if (waiting_pass.contains(pass_name)) {
        if (command_lists.contains(pass_queue_type)) {
          ExecuteCommandList(command_lists.at(pass_queue_type), 1, command_queue.Get(pass_queue_type), &command_list_pool);
          command_lists.erase(pass_queue_type);
        }
        for (auto& [signal_queue, signal_val] : waiting_pass.at(pass_name)) {
          command_queue.RegisterWaitOnQueue(signal_queue, signal_val, pass_queue_type);
        }
      }
      if (barriers.barrier_before_pass.contains(pass_name)) {
        RetainCommandList(pass_queue_type, &command_allocator, &command_lists, &command_list_pool, &shader_visible_descriptor_heap, &allocators);
        ExecuteBarrier(barriers.barrier_before_pass.at(pass_name), buffers.physical_buffer, command_lists.at(pass_queue_type)[0], &buffers.current_buffer_state_list, memory_resource_tmp.get());
      }
      {
        RetainCommandList(pass_queue_type, &command_allocator, &command_lists, &command_list_pool, &shader_visible_descriptor_heap, &allocators);
        auto gpu_handle_ptr = buffers.gpu_descriptor_handles.contains(pass_name) ? buffers.gpu_descriptor_handles.at(pass_name) : D3D12_GPU_DESCRIPTOR_HANDLE{};
        auto cpu_handle_ptr = buffers.cpu_descriptor_handles.contains(pass_name) ? buffers.cpu_descriptor_handles.at(pass_name).data() : nullptr;
        auto resource_ptr   = buffers.pass_resources.contains(pass_name) ? buffers.pass_resources.at(pass_name).data() : nullptr;
        render_functions.at(pass_name)(command_lists.at(pass_queue_type)[0], gpu_handle_ptr, cpu_handle_ptr, resource_ptr);
      }
      if (barriers.barrier_after_pass.contains(pass_name)) {
        RetainCommandList(pass_queue_type, &command_allocator, &command_lists, &command_list_pool, &shader_visible_descriptor_heap, &allocators);
        ExecuteBarrier(barriers.barrier_after_pass.at(pass_name), buffers.physical_buffer, command_lists.at(pass_queue_type)[0], &buffers.current_buffer_state_list, memory_resource_tmp.get());
      }
      if (pass_signal_info.contains(pass_name)) {
        ExecuteCommandList(command_lists.at(pass_queue_type), 1, command_queue.Get(pass_queue_type), &command_list_pool);
        command_lists.erase(pass_queue_type);
        command_queue.RegisterSignal(pass_queue_type, ++pass_signal_val[pass_queue_type]);
        for (auto& waiting_pass_name : pass_signal_info.at(pass_name)) {
          if (!waiting_pass.contains(waiting_pass_name)) {
            waiting_pass.insert({waiting_pass_name, std::pmr::unordered_map<CommandQueueType, uint64_t>{memory_resource_tmp.get()}});
          }
          waiting_pass.at(waiting_pass_name).insert({pass_queue_type, pass_signal_val[pass_queue_type]});
        }
      }
    }
    auto present_signal_val = ++pass_signal_val[CommandQueueType::kGraphics];
    frame_wait_signal[frame_index][CommandQueueType::kGraphics] = present_signal_val;
    for (auto& [command_queue_type, command_list] : command_lists) {
      ExecuteCommandList(command_list, 1, command_queue.Get(command_queue_type), &command_list_pool);
      if (command_queue_type != CommandQueueType::kGraphics) {
        command_queue.RegisterWaitOnQueue(CommandQueueType::kGraphics, present_signal_val, command_queue_type);
      }
    }
    command_lists.clear();
    swapchain.Present();
    command_queue.RegisterSignal(CommandQueueType::kGraphics, present_signal_val);
    barriers = std::move(next_frame_barriers);
    memory_resource_tmp_max_used_bytes = (memory_resource_tmp_max_used_bytes < memory_resource_tmp->GetOffset()) ? memory_resource_tmp->GetOffset() : memory_resource_tmp_max_used_bytes;
    memory_resource_tmp->Reset();
    memory_resource_double_buffered_max_used_bytes = (memory_resource_double_buffered_max_used_bytes < memory_resource_double_buffered->GetOffset()) ? memory_resource_double_buffered->GetOffset() : memory_resource_double_buffered_max_used_bytes;
    memory_resource_double_buffered->Reset();
  }
  command_queue.WaitAll();
  if (term_func) term_func();
  for (auto& [buffer_id, allocation] : buffers.physical_allocation) {
    if (allocation == nullptr) continue;
    allocation->Release();
    buffers.physical_buffer.at(buffer_id)->Release();
  }
  buffers.physical_buffer_allocator->Release();
  while (!allocators.empty()) {
    for (auto a : allocators.back()) {
      command_allocator.ReturnCommandAllocator(a);
    }
    allocators.pop_back();
  }
  descriptor_heap_buffers.Term();
  descriptor_heap_rtv.Term();
  descriptor_heap_dsv.Term();
  command_list_pool.Term();
  command_allocator.Term();
  shader_compiler.Term();
  shader_visible_descriptor_heap.Term();
  swapchain.Term();
  window.Term();
  command_queue.Term();
  device.Term();
  dxgi_core.Term();
  loginfo("<memory stat> main:{}/{}({:.2f}%) per-frame:{}/{}({:.2f}%) double-buffer:{}/{}({:.2f}%)",
          memory_resource->GetOffset(), memory_resource->GetBufferSizeInByte(), static_cast<float>(memory_resource->GetOffset()) / memory_resource->GetBufferSizeInByte() * 100.0f,
          memory_resource_tmp_max_used_bytes, memory_resource_tmp->GetBufferSizeInByte(), static_cast<float>(memory_resource_tmp_max_used_bytes) / memory_resource_tmp->GetBufferSizeInByte() * 100.0f,
          memory_resource_double_buffered_max_used_bytes, memory_resource_double_buffered->GetBufferSizeInByte(), static_cast<float>(memory_resource_double_buffered_max_used_bytes) / memory_resource_double_buffered->GetBufferSizeInByte() * 100.0f);
}
