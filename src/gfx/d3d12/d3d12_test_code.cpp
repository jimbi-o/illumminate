#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "d3d12_descriptor_heap.h"
#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_minimal_for_cpp.h"
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
namespace {
const uint32_t buffer_size_in_bytes = 32 * 1024;
std::byte buffer[buffer_size_in_bytes]{};
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
void ExecuteBarrier(const std::pmr::vector<BarrierConfig>& barrier_info_list, const std::pmr::unordered_map<BufferId, ID3D12Resource*>& physical_buffer, D3d12CommandList* const command_list, std::pmr::memory_resource* memory_resource) {
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
      view_desc.Texture2DArray.FirstArraySlice = 0;
      view_desc.Texture2DArray.ArraySize = desc.buffer_num_to_render;
      view_desc.Texture2DArray.PlaneSlice = desc.index_to_render; // not sure w/FirstArraySlice
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
  // TODO
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
      view_desc.Texture2DArray.FirstArraySlice = 0;
      view_desc.Texture2DArray.ArraySize = desc.buffer_num_to_render;
      view_desc.Texture2DArray.PlaneSlice = desc.index_to_render; // not sure w/FirstArraySlice
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
}
TEST_CASE("d3d12/render") {
  const uint32_t buffer_num = 2;
  const uint32_t swapchain_buffer_num = buffer_num + 1;
  const BufferSize2d swapchain_size{1600, 900};
  const auto mainbuffer_size = swapchain_size;
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
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
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.Get()));
  CommandList command_list_pool;
  CHECK(command_list_pool.Init(device.Get()));
  std::vector<std::vector<ID3D12CommandAllocator**>> allocators(buffer_num);
  SUBCASE("clear swapchain rtv@graphics queue") {
    RenderPassList render_pass_list{memory_resource.get()};
    {
      render_pass_list.push_back(RenderPass(
          StrId("mainpass"),
          {
            {
              BufferConfig(StrId("swapchain"), BufferStateType::kRtv),
            },
            memory_resource.get()
          }
      ));
      render_pass_list.push_back(RenderPass(
          StrId("present"),
          {
            {
              BufferConfig(StrId("swapchain"), BufferStateType::kPresent),
            },
            memory_resource.get()
          }
      ));
    }
    using RenderFunction = std::function<void(D3d12CommandList* const, const D3D12_CPU_DESCRIPTOR_HANDLE)>;
    std::pmr::unordered_map<StrId, RenderFunction> render_functions{memory_resource.get()};
    {
      render_functions.insert({StrId("mainpass"), [](D3d12CommandList* const command_list, const D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle){
        const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
      }});
      render_functions.insert({StrId("present"), [&swapchain]([[maybe_unused]]D3d12CommandList* const command_list, [[maybe_unused]]const D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle){
        swapchain.Present();
      }});
    }
    auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
    auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
    MandatoryOutputBufferNameList named_buffer_list{memory_resource.get()};
    named_buffer_list.insert({StrId("swapchain")});
    auto named_buffers = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, named_buffer_list, memory_resource.get());
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    buffer_state_before_render_pass_list.insert({named_buffers.at(StrId("swapchain")), kBufferStateFlagPresent});
    auto barrier = ConfigureBarrier(render_pass_id_map, render_pass_order, {}, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    auto queue_type = CommandQueueType::kGraphics;
    uint64_t signal_val = 0;
    command_queue.WaitOnCpu({{queue_type, signal_val}});
    for (auto a : allocators.front()) {
      command_allocator.ReturnCommandAllocator(a);
    }
    allocators.front().clear();
    std::rotate(allocators.begin(), allocators.begin() + 1, allocators.end());
    swapchain.UpdateBackBufferIndex();
    std::pmr::unordered_map<BufferId, ID3D12Resource*> physical_buffer{memory_resource.get()};
    physical_buffer.insert({named_buffers.at(StrId("swapchain")), swapchain.GetResource()});
    D3d12CommandList** command_lists = nullptr;
    {
      auto command_allocators = command_allocator.RetainCommandAllocator(queue_type, 1);
      command_lists = command_list_pool.RetainCommandList(queue_type, 1, command_allocators);
      allocators.back().push_back(std::move(command_allocators));
    }
    for (auto& pass_name : render_pass_order) {
      if (pass_name == StrId("present")) {
        command_lists[0]->Close();
        command_queue.Get(queue_type)->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(command_lists));
        command_list_pool.ReturnCommandList(command_lists);
      }
      if (barrier.barrier_before_pass.contains(pass_name)) {
        ExecuteBarrier(barrier.barrier_before_pass.at(pass_name), physical_buffer, command_lists[0], memory_resource.get());
      }
      render_functions.at(pass_name)(command_lists[0], swapchain.GetRtvHandle());
      if (barrier.barrier_after_pass.contains(pass_name)) {
        ExecuteBarrier(barrier.barrier_after_pass.at(pass_name), physical_buffer, command_lists[0], memory_resource.get());
      }
    }
  }
  SUBCASE("clear swapchain uav@compute queue") {
    // swapchain can only be used as rtv (no uav, dsv, copy_dst, etc.)
    RenderPassList render_pass_list{memory_resource.get()};
    {
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
              BufferConfig(StrId("mainbuffer"), BufferStateType::kSrv),
              BufferConfig(StrId("swapchain"), BufferStateType::kRtv),
            },
            memory_resource.get()
          }
      ));
      render_pass_list.push_back(RenderPass(
          StrId("present"),
          {
            {
              BufferConfig(StrId("swapchain"), BufferStateType::kPresent),
            },
            memory_resource.get()
          }
      ));
    }
    using RenderFunction = std::function<void(D3d12CommandList* const, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, ID3D12Resource** resource)>;
    std::pmr::unordered_map<StrId, RenderFunction> render_functions{memory_resource.get()};
    {
      render_functions.insert({StrId("mainpass"), [](D3d12CommandList* const command_list, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, ID3D12Resource** resource){
        // TODO
        const UINT clear_color[4]{255,255,0,255};
        command_list->ClearUnorderedAccessViewUint(gpu_handle, *cpu_handle, *resource, clear_color, 0, nullptr);
      }});
      render_functions.insert({StrId("copy"), [](D3d12CommandList* const command_list, [[maybe_unused]]const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, [[maybe_unused]]ID3D12Resource** resource){
        // TODO
        const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
        command_list->ClearRenderTargetView(*cpu_handle, clear_color, 0, nullptr);
      }});
      render_functions.insert({StrId("present"), [&swapchain]([[maybe_unused]]D3d12CommandList* const command_list, [[maybe_unused]]const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, [[maybe_unused]]const D3D12_CPU_DESCRIPTOR_HANDLE* cpu_handle, [[maybe_unused]]ID3D12Resource** resource){
        swapchain.Present();
      }});
    }
    auto [render_pass_id_map, render_pass_order] = FormatRenderPassList(std::move(render_pass_list), memory_resource.get());
    auto buffer_id_list = CreateBufferIdList(render_pass_id_map, render_pass_order, memory_resource.get());
    MandatoryOutputBufferNameList named_buffer_list{memory_resource.get()};
    named_buffer_list.insert({StrId("swapchain")});
    auto named_buffers = IdentifyMandatoryOutputBufferId(render_pass_id_map, render_pass_order, buffer_id_list, named_buffer_list, memory_resource.get());
    {
      // cull unused render pass
      auto adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
      auto consumer_producer_render_pass_map = CreateConsumerProducerMap(adjacency_graph, memory_resource.get());
      auto used_render_pass_list = GetBufferProducerPassList(adjacency_graph, CreateValueSetFromMap(named_buffers, memory_resource.get()), memory_resource.get());
      used_render_pass_list = GetUsedRenderPassList(std::move(used_render_pass_list), consumer_producer_render_pass_map);
      render_pass_order = CullUnusedRenderPass(std::move(render_pass_order), used_render_pass_list, render_pass_id_map);
      buffer_id_list = RemoveUnusedBuffers(render_pass_order, std::move(buffer_id_list));
    }
    PassSignalInfo pass_signal_info;
    {
      // queue signal, wait info
      auto adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
      auto consumer_producer_render_pass_map = CreateConsumerProducerMap(adjacency_graph, memory_resource.get());
      auto [async_compute_batching, render_pass_unprocessed] = ConfigureAsyncComputeBatching(render_pass_id_map, std::move(render_pass_order), {}, {}, memory_resource.get());
      auto pass_signal_info_resource = ConfigureBufferResourceDependency(render_pass_id_map, async_compute_batching, consumer_producer_render_pass_map, memory_resource.get());
      auto pass_signal_info_batch = ConvertBatchToSignalInfo(async_compute_batching, render_pass_id_map, memory_resource.get());
      pass_signal_info = MergePassSignalInfo(std::move(pass_signal_info_resource), std::move(pass_signal_info_batch));
      render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(async_compute_batching), memory_resource.get());
    }
    auto queue_type = CommandQueueType::kGraphics;
    uint64_t signal_val = 0;
    command_queue.WaitOnCpu({{queue_type, signal_val}});
    for (auto a : allocators.front()) {
      command_allocator.ReturnCommandAllocator(a);
    }
    allocators.front().clear();
    std::rotate(allocators.begin(), allocators.begin() + 1, allocators.end());
    swapchain.UpdateBackBufferIndex();
    PhysicalBufferList physical_buffer{memory_resource.get()};
    std::pmr::unordered_map<StrId, D3D12_GPU_DESCRIPTOR_HANDLE> gpu_descriptor_handles{memory_resource.get()};
    std::pmr::unordered_map<StrId, std::pmr::vector<D3D12_CPU_DESCRIPTOR_HANDLE>> cpu_descriptor_handles{memory_resource.get()};
    std::pmr::unordered_map<StrId, std::pmr::vector<ID3D12Resource*>> pass_resources{memory_resource.get()};
    PassBarrierInfoSet barrier;
    {
      // prepare physical buffers
      physical_buffer.insert({named_buffers.at(StrId("swapchain")), swapchain.GetResource()});
      auto buffer_creation_descs = ConfigureBufferCreationDescs(render_pass_id_map, render_pass_order, buffer_id_list, mainbuffer_size, swapchain_size, memory_resource.get());
      auto buffer_state_before_render_pass_list = CreateBufferCreationStateList(buffer_creation_descs, memory_resource.get());
      auto [physical_buffer_size_in_byte, physical_buffer_alignment] = GetPhysicalBufferSizes(buffer_creation_descs, device.Get(), memory_resource.get());
      CHECK(physical_buffer_size_in_byte.size() == 2);
      CHECK(physical_buffer_size_in_byte.begin()->second > 0);
      CHECK(physical_buffer_alignment.size() == 2);
      CHECK(physical_buffer_alignment.begin()->second > 0);
      // TODO aliasing barrier
      auto [physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass] = CalculatePhysicalBufferLiftime(render_pass_order, buffer_id_list, memory_resource.get());
      auto physical_buffer_address_offset = GetPhysicalBufferAddressOffset(render_pass_order, physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass, physical_buffer_size_in_byte, physical_buffer_alignment, memory_resource.get());
      auto physical_buffer_allocator = CreateMemoryHeapAllocator(device.Get(), dxgi_core.GetAdapter());
      PhysicalAllocationList physical_allocation;
      std::tie(physical_allocation, physical_buffer) = CreatePhysicalBuffers(buffer_creation_descs, physical_buffer_size_in_byte, physical_buffer_alignment, physical_buffer_address_offset, std::move(physical_buffer), physical_buffer_allocator, memory_resource.get());
      // prepare pass buffer handles and resources
      std::pmr::unordered_map<BufferId, std::pmr::unordered_map<BufferStateType, D3D12_CPU_DESCRIPTOR_HANDLE>> cpu_descriptor_handles_per_buffer{memory_resource.get()};
      const uint32_t descriptor_handle_num = 8;
      DescriptorHeap descriptor_heap_buffers, descriptor_heap_rtv, descriptor_heap_dsv;
      CHECK(descriptor_heap_buffers.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, descriptor_handle_num));
      CHECK(descriptor_heap_rtv.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, descriptor_handle_num));
      CHECK(descriptor_heap_dsv.Init(device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, descriptor_handle_num));
      for (auto& [pass_name, buffer_ids] : buffer_id_list) {
        auto& pass = render_pass_id_map.at(pass_name);
        std::pmr::vector<D3D12_CPU_DESCRIPTOR_HANDLE> handles_to_copy_to_gpu{memory_resource.get()};
        for (uint32_t i = 0; i < buffer_ids.size(); i++) {
          auto& buffer_id = buffer_ids[i];
          if (buffer_id == named_buffers.at(StrId("swapchain"))) {
            if (pass.buffer_list[i].state_type == BufferStateType::kRtv) {
              cpu_descriptor_handles[pass_name].push_back(swapchain.GetRtvHandle());
            }
          } else {
            // TODO
            auto& resource = physical_buffer.at(buffer_id);
            auto& buffer_config = pass.buffer_list[i];
            switch (pass.buffer_list[i].state_type) {
              case BufferStateType::kCbv:
              case BufferStateType::kSrv:
              case BufferStateType::kUav: {
                if (!cpu_descriptor_handles_per_buffer.contains(buffer_id)) {
                  cpu_descriptor_handles_per_buffer.insert({buffer_id, std::pmr::unordered_map<BufferStateType, D3D12_CPU_DESCRIPTOR_HANDLE>{memory_resource.get()}});
                }
                auto state_type = pass.buffer_list[i].state_type;
                if (!cpu_descriptor_handles_per_buffer.at(buffer_id).contains(state_type)) {
                  auto handle = descriptor_heap_buffers.RetainHandle();
                  cpu_descriptor_handles_per_buffer.at(buffer_id).insert({state_type, handle});
                  switch (state_type) {
                    case BufferStateType::kCbv: {
                      D3D12_CONSTANT_BUFFER_VIEW_DESC desc{resource->GetGPUVirtualAddress(), physical_buffer_size_in_byte.at(buffer_id)};
                      device.Get()->CreateConstantBufferView(&desc, handle);
                      break;
                    }
                    case BufferStateType::kSrv: {
                      auto desc = GetD3d12ShaderResourceViewDesc(buffer_config, resource);
                      device.Get()->CreateShaderResourceView(resource, &desc, handle);
                      break;
                    }
                    case BufferStateType::kUav: {
                      auto desc = GetD3d12UnorderedAccessViewDesc(buffer_config);
                      device.Get()->CreateUnorderedAccessView(resource, nullptr, &desc, handle);
                      break;
                    }
                    case BufferStateType::kRtv:
                    case BufferStateType::kDsv:
                    case BufferStateType::kCopySrc:
                    case BufferStateType::kCopyDst:
                    case BufferStateType::kPresent:
                      // unreachable
                      break;
                  }
                }
                handles_to_copy_to_gpu.push_back(cpu_descriptor_handles_per_buffer.at(buffer_id).at(state_type));
                break;
              }
              case BufferStateType::kRtv: {
                // make handle in cpu region if not created yet
                // create view
                // push back to cpu_descriptor_handles (always)
                break;
              }
              case BufferStateType::kDsv: {
                // make handle in cpu region if not created yet
                // create view
                // push back to cpu_descriptor_handles (always)
                break;
              }
              case BufferStateType::kCopySrc:
              case BufferStateType::kCopyDst: {
                // push back to pass_resources (always)
                break;
              }
              case BufferStateType::kPresent: {
                break;
              }
            }
          }
        }
        if (!handles_to_copy_to_gpu.empty()) {
          auto gpu_handle = shader_visible_descriptor_heap.CopyToBufferDescriptorHeap(handles_to_copy_to_gpu.data(), static_cast<uint32_t>(handles_to_copy_to_gpu.size()), memory_resource.get());
          gpu_descriptor_handles.insert({pass_name, gpu_handle});
        }
      }
      descriptor_heap_buffers.Term();
      descriptor_heap_rtv.Term();
      descriptor_heap_dsv.Term();
      // barrier configuration
      buffer_state_before_render_pass_list.insert({named_buffers.at(StrId("swapchain")), kBufferStateFlagPresent});
      barrier = ConfigureBarrier(render_pass_id_map, render_pass_order, {}, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
    }
    std::pmr::unordered_map<CommandQueueType, uint64_t> pass_signal_val;
    std::pmr::unordered_map<StrId, std::pmr::unordered_map<CommandQueueType, uint64_t>> waiting_pass;
    std::pmr::unordered_map<CommandQueueType, D3d12CommandList**> command_lists;
    for (auto& pass_name : render_pass_order) {
      auto pass_queue_type = render_pass_id_map.at(pass_name).command_queue_type;
      if (waiting_pass.contains(pass_name)) {
        for (auto& [signal_queue, signal_val] : waiting_pass.at(pass_name)) {
          command_queue.RegisterWaitOnQueue(signal_queue, signal_val, pass_queue_type);
        }
      }
      if (!command_lists.contains(pass_queue_type)) {
        auto command_allocators = command_allocator.RetainCommandAllocator(pass_queue_type, 1);
        command_lists.insert({pass_queue_type, command_list_pool.RetainCommandList(pass_queue_type, 1, command_allocators)});
        shader_visible_descriptor_heap.SetDescriptorHeapsToCommandList(command_lists[pass_queue_type][0]);
        allocators.back().push_back(std::move(command_allocators));
      }
      auto barrier_before_render_funcition_exists = barrier.barrier_before_pass.contains(pass_name);
      if (barrier_before_render_funcition_exists) {
        ExecuteBarrier(barrier.barrier_before_pass.at(pass_name), physical_buffer, command_lists.at(pass_queue_type)[0], memory_resource.get());
      }
      if (barrier_before_render_funcition_exists && pass_name == StrId("present")) {
        ExecuteCommandList(command_lists.at(pass_queue_type), 1, command_queue.Get(pass_queue_type), &command_list_pool);
        if (barrier.barrier_after_pass.contains(pass_name)) {
          auto command_allocators = command_allocator.RetainCommandAllocator(pass_queue_type, 1);
          command_lists.insert({pass_queue_type, command_list_pool.RetainCommandList(pass_queue_type, 1, command_allocators)});
          shader_visible_descriptor_heap.SetDescriptorHeapsToCommandList(command_lists[pass_queue_type][0]);
          allocators.back().push_back(std::move(command_allocators));
        } else {
          command_lists.erase(pass_queue_type);
        }
      }
      {
        auto gpu_handle_ptr = gpu_descriptor_handles.contains(pass_name) ? gpu_descriptor_handles.at(pass_name) : D3D12_GPU_DESCRIPTOR_HANDLE{};
        auto cpu_handle_ptr = cpu_descriptor_handles.contains(pass_name) ? cpu_descriptor_handles.at(pass_name).data() : nullptr;
        auto resource_ptr   = pass_resources.contains(pass_name) ? pass_resources.at(pass_name).data() : nullptr;
        render_functions.at(pass_name)(command_lists.at(pass_queue_type)[0], gpu_handle_ptr, cpu_handle_ptr, resource_ptr);
      }
      if (barrier.barrier_after_pass.contains(pass_name)) {
        ExecuteBarrier(barrier.barrier_after_pass.at(pass_name), physical_buffer, command_lists.at(pass_queue_type)[0], memory_resource.get());
      }
      if (pass_signal_info.contains(pass_name)) {
        ExecuteCommandList(command_lists.at(pass_queue_type), 1, command_queue.Get(pass_queue_type), &command_list_pool);
        command_lists.erase(pass_queue_type);
        command_queue.RegisterSignal(pass_queue_type, ++pass_signal_val[pass_queue_type]);
        for (auto& waiting_pass_name : pass_signal_info.at(pass_name)) {
          if (!waiting_pass.contains(waiting_pass_name)) {
            waiting_pass.insert({waiting_pass_name, std::pmr::unordered_map<CommandQueueType, uint64_t>{memory_resource.get()}});
          }
          waiting_pass.at(waiting_pass_name).insert({pass_queue_type, pass_signal_val[pass_queue_type]});
        }
      }
    }
  }
  SUBCASE("fill swapchain uav with shader@compute queue") {
    // TODO
  }
  SUBCASE("clear + draw triangle to swapchain w/dsv") {
    // TODO
  }
  SUBCASE("clear + draw moving triangle to swapchain w/dsv") {
    // TODO
  }
  SUBCASE("clear + draw moving triangle to rtv w/dsv, copy to uav@compute queue") {
    // TODO
  }
  SUBCASE("transfer texture from cpu and use@graphics queue") {
    // TODO
  }
  SUBCASE("exec frame multiple times") {
    // TODO
  }
  while (!allocators.empty()) {
    for (auto a : allocators.back()) {
      command_allocator.ReturnCommandAllocator(a);
    }
    allocators.pop_back();
  }
  command_queue.WaitAll();
  command_list_pool.Term();
  command_allocator.Term();
  shader_visible_descriptor_heap.Term();
  swapchain.Term();
  window.Term();
  command_queue.Term();
  device.Term();
  dxgi_core.Term();
}