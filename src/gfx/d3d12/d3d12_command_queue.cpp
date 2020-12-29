#include "d3d12_command_queue.h"
#include <limits>
#include "gfx/win32/win32_handle.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool CommandQueue::Init(D3d12Device* const device) {
  device_ = device;
  D3D12_COMMAND_QUEUE_DESC desc = {};
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.NodeMask = 0; // for multi-GPU
  for (auto& type : kCommandQueueTypeSet) {
    desc.Type = ConvertToD3d12CommandQueueType(type);
    ID3D12CommandQueue* command_queue = nullptr;
    auto hr = device_->CreateCommandQueue(&desc, IID_PPV_ARGS(&command_queue));
    ASSERT(SUCCEEDED(hr) && "CreateCommandQueue failed");
    command_queue_[type] = command_queue;
    SET_NAME(command_queue_[type], "commandqueue", desc.Type);
    ID3D12Fence* fence = nullptr;
    hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    ASSERT(SUCCEEDED(hr) && "CreateCommandQueue failed");
    fence_[type] = fence;
    SET_NAME(fence_[type], "fence", desc.Type);
  }
  handle_ = illuminate::gfx::win32::CreateEventHandle();
  ASSERT(handle_ && "create handle failed.");
  return true;
}
void CommandQueue::Term() {
  for (auto& type : kCommandQueueTypeSet) {
    command_queue_[type]->Release();
    fence_[type]->Release();
  }
  if (handle_) {
    illuminate::gfx::win32::CloseHandle(handle_);
  }
}
void CommandQueue::RegisterSignal(const CommandQueueType command_queue_type, const uint64_t val) {
  auto hr = command_queue_.at(command_queue_type)->Signal(fence_.at(command_queue_type), val);
  if (FAILED(hr)) {
    logerror("command queue signal failed. {} {} {}", hr, command_queue_type, val);
  }
  ASSERT(SUCCEEDED(hr) && "command queue signal failed.");
}
void CommandQueue::RegisterWaitOnQueue(const CommandQueueType signal_queue, const uint64_t val, const CommandQueueType waiting_queue) {
  ASSERT(signal_queue != waiting_queue);
  auto hr = command_queue_.at(waiting_queue)->Wait(fence_.at(signal_queue), val);
  if (FAILED(hr)) {
    logerror("command queue wait failed. {} {} {} {}", hr, waiting_queue, val, signal_queue);
  }
  ASSERT(SUCCEEDED(hr) && "command queue wait failed.");
}
void CommandQueue::WaitOnCpu(std::unordered_map<CommandQueueType, uint64_t>&& signals) {
  for (auto& queue_type : kCommandQueueTypeSet) {
    if (!signals.contains(queue_type)) continue;
    auto comp_val = fence_[queue_type]->GetCompletedValue();
    auto signal_val = signals[queue_type];
    logtrace("fence@{}. comp:{} wait:{}", queue_type, comp_val, signal_val);
    if (comp_val < signal_val) {
      loginfo("cpu bound wait on queue@{}. comp:{} wait:{}", queue_type, comp_val, signal_val);
      continue;
    }
    signals.erase(queue_type);
  }
  if (signals.empty()) return;
  if (signals.size() > 1) {
    std::vector<ID3D12Fence*> fences(signals.size());
    std::vector<uint64_t> sig(signals.size());
    while (!signals.empty()) {
      auto it = signals.begin();
      fences.push_back(fence_[it->first]);
      sig.push_back(it->second);
      signals.erase(it);
    }
    auto hr = device_->SetEventOnMultipleFenceCompletion(fences.data(), sig.data(), static_cast<uint32_t>(sig.size()), D3D12_MULTIPLE_FENCE_WAIT_FLAG_ALL, handle_);
    ASSERT(SUCCEEDED(hr) && "SetEventOnMultipleFenceCompletion failed");
  } else {
    auto it = signals.begin();
    auto hr = fence_[it->first]->SetEventOnCompletion(it->second, handle_);
    ASSERT(SUCCEEDED(hr) && "SetEventOnCompletion failed");
  }
  auto hr = WaitForSingleObject(handle_, INFINITE);
  ASSERT(SUCCEEDED(hr) && "WaitForSingleObject failed");
}
void CommandQueue::WaitAll() {
  auto signal_val = std::numeric_limits<uint64_t>::max();
  for (auto& queue_type : kCommandQueueTypeSet) {
    RegisterSignal(queue_type, signal_val);
    WaitOnCpu({{queue_type, signal_val}});
    RegisterSignal(queue_type, 0);
  }
}
}
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
TEST_CASE("command queue") {
  using namespace illuminate::gfx::d3d12;
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  CommandQueue command_queue;
  command_queue.Init(device.Get());
  SUBCASE("cpu bound (frame bufferred)") {
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 1);
    command_queue.WaitOnCpu({{CommandQueueType::kGraphics, 1},});
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 101);
    command_queue.RegisterSignal(CommandQueueType::kCompute,  102);
    command_queue.WaitOnCpu({{CommandQueueType::kGraphics, 101}, {CommandQueueType::kCompute, 102},});
  }
  SUBCASE("gpu bound") { // e.g. graphics<->compute sync, wait for texture buffer transferred to GPU
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 1);
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 2);
    command_queue.RegisterWaitOnQueue(CommandQueueType::kGraphics, 1, CommandQueueType::kCompute);
    command_queue.RegisterSignal(CommandQueueType::kCompute,  3);
    command_queue.RegisterWaitOnQueue(CommandQueueType::kCompute, 3, CommandQueueType::kGraphics);
  }
  SUBCASE("single frame and multi frame mixed") {
    command_queue.RegisterSignal(CommandQueueType::kTransfer, 1);
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 1);
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 2);
    command_queue.RegisterWaitOnQueue(CommandQueueType::kGraphics, 1, CommandQueueType::kCompute);
    command_queue.RegisterSignal(CommandQueueType::kCompute,  3);
    command_queue.RegisterWaitOnQueue(CommandQueueType::kCompute,  3, CommandQueueType::kGraphics);
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 3);
    command_queue.WaitOnCpu({{CommandQueueType::kGraphics, 3},});
    command_queue.RegisterWaitOnQueue(CommandQueueType::kTransfer, 1, CommandQueueType::kGraphics);
    command_queue.RegisterSignal(CommandQueueType::kGraphics, 4);
    command_queue.RegisterSignal(CommandQueueType::kCompute,  4);
    command_queue.WaitOnCpu({{CommandQueueType::kGraphics, 4}, {CommandQueueType::kCompute, 4}});
  }
  command_queue.Term();
  device.Term();
  dxgi_core.Term();
}
