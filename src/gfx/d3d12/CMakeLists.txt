cmake_minimum_required(VERSION 3.15)
project(illuminategfxd3d12
  VERSION 1.0
  DESCRIPTION "d3d12 renderer"
  LANGUAGES CXX
)

include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/onqtam/doctest.git
  GIT_TAG        d5aa2bfb8f00b6260296a754af3a3a98d93f7b67
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(doctest)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        39150eb8c7673f37d740d957acfcbff8c48ef9a6
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(spdlog)
FetchContent_Declare(
  d3dx12
  GIT_REPOSITORY https://github.com/microsoft/DirectX-Graphics-Samples.git
  GIT_TAG        58b660720c61b0310301679fb1d5945a7bce9ebc
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(d3dx12)

add_subdirectory("../win32" "${CMAKE_CURRENT_BINARY_DIR}/gfx/win32")

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  if (NOT DEFINED BUILD_WITH_TEST)
	set(BUILD_WITH_TEST ON)
  endif()
  add_executable(illuminategfxd3d12)
  if (BUILD_WITH_TEST)
	target_sources(illuminategfxd3d12 PRIVATE "d3d12_test.cpp")
  endif()
else()
  add_library(illuminategfxd3d12)
endif()

target_compile_features(illuminategfxd3d12 PUBLIC cxx_std_17)
if (NOT MSVC)
  target_compile_options(illuminategfxd3d12 PRIVATE -msse4.2)
endif()
if (MSVC)
  target_compile_definitions(illuminategfxd3d12 PRIVATE NOMINMAX)
endif()
if ("${CMAKE_VS_PLATFORM_TOOLSET}" STREQUAL "ClangCL")
  target_compile_options(illuminategfxd3d12 PRIVATE "/arch:AVX2")
endif()
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminategfxd3d12 PRIVATE BUILD_WITH_TEST)
  target_link_libraries(illuminategfxd3d12 PRIVATE doctest::doctest)
else()
  target_compile_definitions(illuminategfxd3d12 PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_link_libraries(illuminategfxd3d12 PRIVATE spdlog dxguid.lib illuminategfxwin32)

target_include_directories(illuminategfxd3d12
  PUBLIC
  "../../../include"
  PRIVATE
  "."
  "../"
  "../../"
  "../../../include/illuminate"
  "../../../include/illuminate/gfx"
  "../../../include/illuminate/gfx/d3d12"
  "${doctest_SOURCE_DIR}"
  "${d3dx12_SOURCE_DIR}/Libraries/D3DX12"
)

target_sources(illuminategfxd3d12 PRIVATE
  "d3d12_renderer.cpp"
  "d3d12_dxgi_core.cpp"
  "d3d12_device.cpp"
  "d3d12_util.cpp"
  "d3d12_command_allocator.cpp"
  "d3d12_command_list.cpp"
  "d3d12_command_queue.cpp"
  "d3d12_swapchain.cpp"
)
