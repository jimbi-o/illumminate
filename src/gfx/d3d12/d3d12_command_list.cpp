#include "d3d12_command_list.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool CommandList::Init(D3d12Device* const device) {
  device_ = device;
  return true;
}
void CommandList::Term() {
  for (auto& list : pool_) {
    for (auto& list : list.second) {
      list->Release();
    }
  }
  for (auto& pair : allocation_info_) {
    auto& list = pair.first;
    auto num = std::get<1>(pair.second);
    logwarn("command list not released. {} {}", std::get<0>(pair.second), num);
    for (uint32_t i = 0; i < num; i++) {
      list[i]->Release();
    }
  }
}
D3d12CommandList** CommandList::RetainCommandList(const CommandQueueType command_list_type, const uint32_t num, ID3D12CommandAllocator** const allocators) {
  auto d3d12_command_list_type = ConvertToD3d12CommandQueueType(command_list_type);
  if (pool_[command_list_type].size() < num) {
    loginfo("command list creation. {}({})", num * 2, pool_[command_list_type].size());
    while (pool_[command_list_type].size() < num * 2) {
      D3d12CommandList* list = nullptr;
#if 0
      // somehow fails
      auto hr = device_->CreateCommandList1(0/*multi-GPU*/, d3d12_command_list_type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&list));
      if (FAILED(hr)) {
        logerror("failed to create command list {} {} {} {}", hr, d3d12_command_list_type, num * 2, pool_[command_list_type].size());
        ASSERT(false && "CreateCommandList1 failed.", hr);
      }
#else
      auto hr = device_->CreateCommandList(0/*multi-GPU*/, d3d12_command_list_type, allocators[0], nullptr, IID_PPV_ARGS(&list));
      if (FAILED(hr)) {
        logerror("failed to create command list {} {} {} {}", hr, d3d12_command_list_type, num * 2, pool_[command_list_type].size());
        ASSERT(false && "CreateCommandList failed.", hr);
      }
      list->Close();
#endif
      SET_NAME(list, "commandlist", d3d12_command_list_type * 1000 + pool_[command_list_type].size());
      pool_[command_list_type].push_back(list);
    }
  }
  D3d12CommandList** list = new D3d12CommandList*[num]{};
  for (uint32_t i = 0; i < num; i++) {
    list[i] = pool_[command_list_type].back();
    pool_[command_list_type].pop_back();
    list[i]->Reset(allocators[i], nullptr);
  }
  allocation_info_[list] = std::make_tuple(command_list_type, num);
  return list;
}
void CommandList::ReturnCommandList(D3d12CommandList** const list) {
  const auto command_list_type = std::get<0>(allocation_info_[list]);
  const auto num = std::get<1>(allocation_info_[list]);
  allocation_info_.erase(list);
  for (uint32_t i = 0; i < num; i++) {
    pool_[command_list_type].push_back(list[i]);
  }
}
}
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
#include "d3d12_command_allocator.h"
TEST_CASE("command list") {
  using namespace illuminate::gfx::d3d12;
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.GetDevice()));
  CommandList command_list;
  CHECK(command_list.Init(device.GetDevice()));
  auto command_allocators = command_allocator.RetainCommandAllocator(CommandQueueType::kGraphics, 3);
  auto command_lists = command_list.RetainCommandList(CommandQueueType::kGraphics, 3, command_allocators);
  CHECK(command_lists[0]);
  CHECK(command_lists[1]);
  CHECK(command_lists[2]);
  command_list.ReturnCommandList(command_lists);
  auto command_allocators_c = command_allocator.RetainCommandAllocator(CommandQueueType::kCompute, 4);
  command_lists = command_list.RetainCommandList(CommandQueueType::kCompute, 4, command_allocators_c);
  CHECK(command_lists[0]);
  CHECK(command_lists[1]);
  CHECK(command_lists[2]);
  command_list.ReturnCommandList(command_lists);
  command_allocator.ReturnCommandAllocator(command_allocators);
  command_allocator.ReturnCommandAllocator(command_allocators_c);
  command_list.Term();
  command_allocator.Term();
  device.Term();
  dxgi_core.Term();
}
