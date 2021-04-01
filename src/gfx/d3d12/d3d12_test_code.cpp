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
struct DeviceSet {
  DxgiCore dxgi_core;
  Device device;
  CommandQueue command_queue;
  illuminate::gfx::win32::Window window;
  Swapchain swapchain;
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
};
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
  devices.Term();
  PmrLinearAllocator memory_resource_persistant(&buffer[buffer_size_in_bytes_persistant], buffer_size_in_bytes_persistant);
}
