#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_swapchain.h"
#include "gfx/win32/win32_window.h"
#include "gfx/render_graph.h"
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
}
#endif
TEST_CASE("d3d12/render") {
  const uint32_t buffer_num = 2;
  const uint32_t swapchain_buffer_num = buffer_num + 1;
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
  CHECK(window.Init("swapchain test", 1600, 900));
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
    RenderPassList render_pass_list{memory_resource.get()};
    {
      render_pass_list.push_back(RenderPass(
          StrId("mainpass"),
          {
            {
              BufferConfig(StrId("swapchain"), BufferStateType::kUav),
            },
            memory_resource.get()
          }
      ).CommandQueueTypeCompute());
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
    std::pmr::unordered_set<StrId> command_list_not_used_pass{memory_resource.get()};
    command_list_not_used_pass.insert(StrId("present"));
    using RenderFunction = std::function<void(D3d12CommandList* const, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource* resource)>;
    std::pmr::unordered_map<StrId, RenderFunction> render_functions{memory_resource.get()};
    {
      render_functions.insert({StrId("mainpass"), [](D3d12CommandList* const command_list, const D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle, const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle, ID3D12Resource* resource){
        const UINT clear_color[4]{255,255,0,255};
        command_list->ClearUnorderedAccessViewUint(gpu_handle, cpu_handle, resource, clear_color, 0, nullptr);
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
    auto adjacency_graph = CreateRenderPassAdjacencyGraph(render_pass_id_map, render_pass_order, buffer_id_list, memory_resource.get());
    auto consumer_producer_render_pass_map = CreateConsumerProducerMap(adjacency_graph, memory_resource.get());
    auto [async_compute_batching, render_pass_unprocessed] = ConfigureAsyncComputeBatching(render_pass_id_map, std::move(render_pass_order), {}, {}, memory_resource.get());
    auto pass_signal_info_resource = ConfigureBufferResourceDependency(render_pass_id_map, async_compute_batching, consumer_producer_render_pass_map, memory_resource.get());
    auto pass_signal_info_batch = ConvertBatchToSignalInfo(async_compute_batching, render_pass_id_map, memory_resource.get());
    auto pass_signal_info = MergePassSignalInfo(std::move(pass_signal_info_resource), std::move(pass_signal_info_batch));
    render_pass_order = ConvertBatchInfoBackToRenderPassOrder(std::move(async_compute_batching), memory_resource.get());
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
    std::pmr::unordered_map<CommandQueueType, uint64_t> pass_signal_val;
    std::pmr::unordered_map<StrId, std::pmr::unordered_map<CommandQueueType, uint64_t>> waiting_pass;
    D3d12CommandList** command_lists = nullptr;
    for (auto& pass_name : render_pass_order) {
      auto pass_queue_type = render_pass_id_map.at(pass_name).command_queue_type;
      if (waiting_pass.contains(pass_name)) {
        for (auto& [signal_queue, signal_val] : waiting_pass.at(pass_name)) {
          command_queue.RegisterWaitOnQueue(signal_queue, signal_val, pass_queue_type);
        }
      }
      if (!command_list_not_used_pass.contains(pass_name) && command_lists == nullptr) {
        auto command_allocators = command_allocator.RetainCommandAllocator(pass_queue_type, 1);
        command_lists = command_list_pool.RetainCommandList(pass_queue_type, 1, command_allocators);
        allocators.back().push_back(std::move(command_allocators));
      }
      if (barrier.barrier_before_pass.contains(pass_name)) {
        ExecuteBarrier(barrier.barrier_before_pass.at(pass_name), physical_buffer, command_lists[0], memory_resource.get());
      }
      render_functions.at(pass_name)(command_lists[0], {}/*TODO*/, swapchain.GetRtvHandle(), swapchain.GetResource());
      if (barrier.barrier_after_pass.contains(pass_name)) {
        ExecuteBarrier(barrier.barrier_after_pass.at(pass_name), physical_buffer, command_lists[0], memory_resource.get());
      }
      if (pass_signal_info.contains(pass_name)) {
        command_lists[0]->Close();
        command_queue.Get(pass_queue_type)->ExecuteCommandLists(1, reinterpret_cast<ID3D12CommandList**>(command_lists));
        command_list_pool.ReturnCommandList(command_lists);
        command_lists = nullptr;
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
