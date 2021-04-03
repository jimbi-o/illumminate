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
  uint32_t allocator_buffer_index_;
  uint32_t allocator_buffer_len_;
  vector<vector<ID3D12CommandAllocator**>> allocator_buffer_;
  unordered_map<CommandQueueType, D3d12CommandList**> command_list_in_use_;
  unordered_map<CommandQueueType, uint32_t> command_list_num_in_use_;
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
const uint32_t buffer_offset_in_bytes_persistant = 0;
const uint32_t buffer_size_in_bytes_persistant = 4 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_persistant + buffer_size_in_bytes_persistant;
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
TEST_CASE("clear swapchain buffer") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  const BufferSize2d swapchain_size{1600, 900};
  const uint32_t frame_buffer_num = 2;
  const uint32_t swapchain_buffer_num = frame_buffer_num + 1;
  DeviceSet devices;
  CHECK(devices.Init(frame_buffer_num, swapchain_size, swapchain_buffer_num));
  PmrLinearAllocator memory_resource_persistant(&buffer[buffer_size_in_bytes_persistant], buffer_size_in_bytes_persistant);
  CommandListSet command_list_set(&memory_resource_persistant);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  SignalValues signal_values(&memory_resource_persistant, frame_buffer_num);
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
  PmrLinearAllocator memory_resource_persistant(&buffer[buffer_size_in_bytes_persistant], buffer_size_in_bytes_persistant);
  CommandListSet command_list_set(&memory_resource_persistant);
  CHECK(command_list_set.Init(devices.GetDevice(), frame_buffer_num));
  SignalValues signal_values(&memory_resource_persistant, frame_buffer_num);
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
