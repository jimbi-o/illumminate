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
#ifdef BUILD_WITH_TEST
namespace {
const uint32_t buffer_offset_in_bytes_system = 0;
const uint32_t buffer_size_in_bytes_system = 4 * 1024;
const uint32_t buffer_offset_in_bytes_scene = buffer_offset_in_bytes_system + buffer_size_in_bytes_system;
const uint32_t buffer_size_in_bytes_scene = 4 * 1024;
const uint32_t buffer_offset_in_bytes_frame = buffer_offset_in_bytes_scene + buffer_size_in_bytes_scene;
const uint32_t buffer_size_in_bytes_frame = 4 * 1024;
const uint32_t buffer_offset_in_bytes_work = buffer_offset_in_bytes_frame + buffer_size_in_bytes_frame;
const uint32_t buffer_size_in_bytes_work = 4 * 1024;
std::byte buffer[buffer_offset_in_bytes_work + buffer_size_in_bytes_work]{};
}
#endif
