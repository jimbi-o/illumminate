#include "d3d12_command_allocator.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
bool CommandAllocator::Init(D3d12Device* const device) {
  device_ = device;
  return true;
}
void CommandAllocator::Term() {
  for (auto& list : pool_) {
    for (auto& allocator : list.second) {
      allocator->Release();
    }
  }
  for (auto& pair : allocation_info_) {
    auto& allocator = pair.first;
    auto num = std::get<1>(pair.second);
    logwarn("command allocator not released. {} {}", std::get<0>(pair.second), num);
    for (uint32_t i = 0; i < num; i++) {
      allocator[i]->Release();
    }
  }
}
ID3D12CommandAllocator** CommandAllocator::RetainCommandAllocator(const CommandQueueType command_list_type, const uint32_t num) {
  auto d3d12_command_list_type = ConvertToD3d12CommandQueueType(command_list_type);
  if (pool_[command_list_type].size() < num) {
    loginfo("command allocator creation. {}({})", num * 2, pool_[command_list_type].size());
    while (pool_[command_list_type].size() < num * 2) {
      ID3D12CommandAllocator* allocator = nullptr;
      auto hr = device_->CreateCommandAllocator(d3d12_command_list_type, IID_PPV_ARGS(&allocator));
      if (FAILED(hr)) {
        logerror("failed to create command allocator {} {} {} {}", hr, d3d12_command_list_type, num * 2, pool_[command_list_type].size());
        ASSERT(false && "CreateCommandAllocator failed.");
      }
      SET_NAME(allocator, "commandallocator", d3d12_command_list_type * 1000 + pool_[command_list_type].size());
      pool_[command_list_type].push_back(allocator);
    }
  }
  ID3D12CommandAllocator** allocator = new ID3D12CommandAllocator*[num]{};
  for (uint32_t i = 0; i < num; i++) {
    allocator[i] = pool_[command_list_type].back();
    pool_[command_list_type].pop_back();
  }
  allocation_info_[allocator] = std::make_tuple(command_list_type, num);
  return allocator;
}
void CommandAllocator::ReturnCommandAllocator(ID3D12CommandAllocator** const allocator) {
  const auto command_list_type = std::get<0>(allocation_info_[allocator]);
  const auto num = std::get<1>(allocation_info_[allocator]);
  allocation_info_.erase(allocator);
  for (uint32_t i = 0; i < num; i++) {
    pool_[command_list_type].push_back(allocator[i]);
  }
}
}
#include "doctest/doctest.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_device.h"
TEST_CASE("command allocator") {
  using namespace illuminate::gfx::d3d12;
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  CommandAllocator command_allocator;
  CHECK(command_allocator.Init(device.Get()));
  auto command_allocators = command_allocator.RetainCommandAllocator(CommandQueueType::kGraphics, 3);
  CHECK(command_allocators[0]);
  CHECK(command_allocators[1]);
  CHECK(command_allocators[2]);
  command_allocator.ReturnCommandAllocator(command_allocators);
  command_allocators = command_allocator.RetainCommandAllocator(CommandQueueType::kGraphics, 3);
  CHECK(command_allocators[0]);
  CHECK(command_allocators[1]);
  CHECK(command_allocators[2]);
  command_allocator.ReturnCommandAllocator(command_allocators);
  command_allocators = command_allocator.RetainCommandAllocator(CommandQueueType::kCompute, 5);
  CHECK(command_allocators[0]);
  CHECK(command_allocators[1]);
  CHECK(command_allocators[2]);
  CHECK(command_allocators[3]);
  CHECK(command_allocators[4]);
  command_allocator.Term();
  device.Term();
  dxgi_core.Term();
}
