#ifndef __ILLUMINATE_D3D12_COMMAND_ALLOCATOR_H__
#define __ILLUMINATE_D3D12_COMMAND_ALLOCATOR_H__
#include "d3d12_header_common.h"
namespace illuminate::gfx::d3d12 {
class CommandAllocator {
 public:
  bool Init(D3d12Device* const);
  void Term();
  ID3D12CommandAllocator** RetainCommandAllocator(const CommandListType, const uint32_t num);
  void ReturnCommandAllocator(ID3D12CommandAllocator** const);
 private:
  D3d12Device* device_ = nullptr;
  std::unordered_map<CommandListType, std::vector<ID3D12CommandAllocator*>> pool_;
  std::unordered_map<ID3D12CommandAllocator**, std::tuple<CommandListType, uint32_t>> allocation_info_;
};
}
#endif
