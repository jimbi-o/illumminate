#ifndef ILLUMINATE_D3D12_COMMAND_LIST_H
#define ILLUMINATE_D3D12_COMMAND_LIST_H
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class CommandList {
 public:
  bool Init(D3d12Device* const device);
  void Term();
  D3d12CommandList** RetainCommandList(const CommandQueueType type, const uint32_t num, ID3D12CommandAllocator** const allocators);
  void ReturnCommandList(D3d12CommandList** const command_list);
 private:
  D3d12Device* device_ = nullptr;
  std::unordered_map<CommandQueueType, std::vector<D3d12CommandList*>> pool_;
  std::unordered_map<D3d12CommandList**, std::tuple<CommandQueueType, uint32_t>> allocation_info_;
};
}
#endif
