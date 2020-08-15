#ifndef __ILLUMINATE_D3D12_COMMAND_QUEUE_H__
#define __ILLUMINATE_D3D12_COMMAND_QUEUE_H__
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class CommandQueue {
 public:
  bool Init(D3d12Device* const device);
  void Term();
  constexpr ID3D12CommandQueue* GetCommandQueue(const CommandListType type) { return command_queue_[type]; }
  void RegisterSignal(const CommandListType, const uint64_t);
  void RegisterWaitOnQueue(const CommandListType signal_queue, const uint64_t, const CommandListType waiting_queue);
  void WaitOnCpu(std::unordered_map<CommandListType, uint64_t>&&);
  void WaitAll();
 private:
  D3d12Device* device_ = nullptr;
  std::unordered_map<CommandListType, ID3D12CommandQueue*> command_queue_;
  std::unordered_map<CommandListType, ID3D12Fence*> fence_;
  HANDLE handle_ = nullptr;
};
}
#endif
