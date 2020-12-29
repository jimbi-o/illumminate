#include "d3d12_command_allocator.h"
#include "d3d12_command_list.h"
#include "d3d12_command_queue.h"
#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_swapchain.h"
#include "gfx/win32/win32_window.h"
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
TEST_CASE("d3d12/render") {
  SUBCASE("clear swapchain rtv@graphics queue") {
    // TODO
  }
  SUBCASE("clear swapchain uav@compute queue") {
    // TODO
  }
  SUBCASE("fill swapchain uav with shader@compute queue") {
    // TODO
  }
  SUBCASE("clear + draw triangle to swapchain uav@compute queue") {
    // TODO
  }
  SUBCASE("clear + draw moving triangle to swapchain uav") {
    // TODO
  }
}
