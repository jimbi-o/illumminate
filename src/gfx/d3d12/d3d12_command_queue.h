#ifndef ILLUMINATE_D3D12_COMMAND_QUEUE_H
#define ILLUMINATE_D3D12_COMMAND_QUEUE_H
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class CommandQueue {
 public:
  bool Init(D3d12Device* const device);
  void Term();
  ID3D12CommandQueue* Get(const CommandQueueType type) { return command_queue_.at(type); }
  void RegisterSignal(const CommandQueueType, const uint64_t);
  void RegisterWaitOnQueue(const CommandQueueType signal_queue, const uint64_t&, const CommandQueueType waiting_queue);
  void WaitOnCpu(const std::pmr::unordered_map<CommandQueueType, uint64_t>&);
  void WaitAll();
 private:
  D3d12Device* device_ = nullptr;
  std::unordered_map<CommandQueueType, ID3D12CommandQueue*> command_queue_;
  std::unordered_map<CommandQueueType, ID3D12Fence*> fence_;
  HANDLE handle_ = nullptr;
};
}
#endif
