#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_swapchain.h"
#include "gfx/win32/win32_window.h"
#include "gfx/render_graph.h"
#include "D3D12MemAlloc.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#ifdef BUILD_WITH_TEST
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
    case BufferDimensionType::kBuffer: return D3D12_RESOURCE_DIMENSION_BUFFER;
    case BufferDimensionType::k1d: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case BufferDimensionType::k1dArray: return D3D12_RESOURCE_DIMENSION_TEXTURE1D;
    case BufferDimensionType::k2d: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case BufferDimensionType::k2dArray: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case BufferDimensionType::k3d: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    case BufferDimensionType::k3dArray: return D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    case BufferDimensionType::kCube: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    case BufferDimensionType::kCubeArray: return D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  }
}
constexpr D3D12_RESOURCE_FLAGS ResourceFlags(const BufferStateFlags state_flags) {
  D3D12_RESOURCE_FLAGS flags{};
  if (!(state_flags & kBufferStateFlagCbv) && !(state_flags & kBufferStateFlagSrv)) flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  if (state_flags & kBufferStateFlagUav) flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  if (state_flags & kBufferStateFlagRtv) flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if ((state_flags & kBufferStateFlagDsvWrite) || (state_flags & kBufferStateFlagDsvRead)) flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  return flags;
}
constexpr D3D12_RESOURCE_DESC GetD3d12ResourceDesc(const BufferCreationDesc& desc) {
  D3D12_RESOURCE_DESC resource_desc{};
  resource_desc.Dimension = Dimension(desc.dimension_type);
  resource_desc.Alignment = 0;
  resource_desc.Width = desc.width;
  resource_desc.Height = desc.height;
  resource_desc.DepthOrArraySize = desc.depth;
  resource_desc.MipLevels = 1; // TODO?
  resource_desc.Format = GetDxgiFormat(desc.format);
  resource_desc.SampleDesc = {1, 0};
  resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags = ResourceFlags(desc.state_flags);
  return resource_desc;
}
std::tuple<std::pmr::unordered_map<BufferId, uint32_t>, std::pmr::unordered_map<BufferId, uint32_t>> GetPhysicalBufferSizes(const BufferCreationDescList& buffer_creation_descs, Device* const device, std::pmr::memory_resource* memory_resource) {
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_size_in_byte{memory_resource};
  std::pmr::unordered_map<BufferId, uint32_t> physical_buffer_alignment{memory_resource};
  for (auto& [id, desc] : buffer_creation_descs) {
    auto resource_desc = GetD3d12ResourceDesc(desc);
    auto alloc_info = device->Get()->GetResourceAllocationInfo(0, 1, &resource_desc);
    physical_buffer_size_in_byte.insert({id, alloc_info.SizeInBytes});
    physical_buffer_alignment.insert({id, alloc_info.Alignment});
  }
  return {physical_buffer_size_in_byte, physical_buffer_alignment};
}
using PhysicalBufferList = std::pmr::unordered_map<BufferId, ID3D12Resource*>;
PhysicalBufferList CreatePhysicalBuffers(const BufferCreationDescList& buffer_creation_descs, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_size_in_byte, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_alignment, const std::pmr::unordered_map<BufferId, uint32_t>& physical_buffer_address_offset, PhysicalBufferList&& physical_buffer) {
  // TODO
  return physical_buffer;
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
}
#endif
TEST_CASE("d3d12/render") {
  const uint32_t buffer_num = 2;
  const uint32_t swapchain_buffer_num = buffer_num + 1;
  const BufferSize2d swapchain_size{1600, 900};
  const auto mainbuffer_size = swapchain_size;
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
    using RenderFunction = std::function<void(D3d12CommandList* const, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource* resource)>;
    std::pmr::unordered_map<StrId, RenderFunction> render_functions{memory_resource.get()};
    {
      render_functions.insert({StrId("mainpass"), [](D3d12CommandList* const command_list, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource* resource){
        // TODO
        const UINT clear_color[4]{255,255,0,255};
        command_list->ClearUnorderedAccessViewUint(gpu_handle, cpu_handle, resource, clear_color, 0, nullptr);
      }});
      render_functions.insert({StrId("copy"), [](D3d12CommandList* const command_list, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource* resource){
        // TODO
        const FLOAT clear_color[4] = {0.0f,1.0f,1.0f,1.0f};
        command_list->ClearRenderTargetView(cpu_handle, clear_color, 0, nullptr);
      }});
      render_functions.insert({StrId("present"), [&swapchain]([[maybe_unused]]D3d12CommandList* const command_list, [[maybe_unused]]const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, [[maybe_unused]]const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, [[maybe_unused]]ID3D12Resource* resource){
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
    BufferStateList buffer_state_before_render_pass_list{memory_resource.get()};
    {
      // prepare physical buffers
      physical_buffer.insert({named_buffers.at(StrId("swapchain")), swapchain.GetResource()});
      auto buffer_creation_descs = ConfigureBufferCreationDescs(render_pass_id_map, render_pass_order, buffer_id_list, mainbuffer_size, swapchain_size, memory_resource.get());
      buffer_state_before_render_pass_list = CreateBufferCreationStateList(buffer_creation_descs, memory_resource.get());
      auto [physical_buffer_size_in_byte, physical_buffer_alignment] = GetPhysicalBufferSizes(buffer_creation_descs, &device, memory_resource.get());
      CHECK(physical_buffer_size_in_byte.size() == 2);
      CHECK(physical_buffer_size_in_byte.begin()->second > 0);
      CHECK(physical_buffer_alignment.size() == 2);
      CHECK(physical_buffer_alignment.begin()->second > 0);
      // TODO aliasing barrier
      auto [physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass] = CalculatePhysicalBufferLiftime(render_pass_order, buffer_id_list, memory_resource.get());
      auto physical_buffer_address_offset = GetPhysicalBufferAddressOffset(render_pass_order, physical_buffer_lifetime_begin_pass, physical_buffer_lifetime_end_pass, physical_buffer_size_in_byte, physical_buffer_alignment, memory_resource.get());
      physical_buffer = CreatePhysicalBuffers(buffer_creation_descs, physical_buffer_size_in_byte, physical_buffer_alignment, physical_buffer_address_offset, std::move(physical_buffer));
    }
    // barrier configuration
    buffer_state_before_render_pass_list.insert({named_buffers.at(StrId("swapchain")), kBufferStateFlagPresent});
    auto barrier = ConfigureBarrier(render_pass_id_map, render_pass_order, {}, buffer_id_list, buffer_state_before_render_pass_list, {}, memory_resource.get());
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
          allocators.back().push_back(std::move(command_allocators));
        } else {
          command_lists.erase(pass_queue_type);
        }
      }
      render_functions.at(pass_name)(command_lists.at(pass_queue_type)[0], {}/*TODO*/, swapchain.GetRtvHandle(), swapchain.GetResource()); // TODO
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
  swapchain.Term();
  window.Term();
  command_queue.Term();
  device.Term();
  dxgi_core.Term();
}
