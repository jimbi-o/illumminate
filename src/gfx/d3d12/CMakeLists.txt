cmake_minimum_required(VERSION 3.15)
project(illuminategfxd3d12
  VERSION 1.0
  DESCRIPTION "d3d12 renderer"
  LANGUAGES CXX
)

include(FetchContent)
FetchContent_Declare(
  d3dx12
  GIT_REPOSITORY https://github.com/microsoft/DirectX-Graphics-Samples.git
  GIT_TAG        58b660720c61b0310301679fb1d5945a7bce9ebc
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(d3dx12)
FetchContent_Declare(
  d3d12memoryallocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git
  GIT_TAG        e56c26d8105a679f22860b865f4c074d19006e88
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(d3d12memoryallocator)
FetchContent_Declare(
  dxcapi
  URL https://ci.appveyor.com/api/projects/antiagainst/directxshadercompiler/artifacts/build%2FRelease%2Fdxc-artifacts.zip?branch=master&pr=false&job=image%3A%20Visual%20Studio%202017
)
FetchContent_MakeAvailable(dxcapi)

add_subdirectory(".." "${CMAKE_CURRENT_BINARY_DIR}/gfx")
add_subdirectory("../win32" "${CMAKE_CURRENT_BINARY_DIR}/win32")

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  if (NOT DEFINED BUILD_WITH_TEST)
	set(BUILD_WITH_TEST ON)
  endif()
  add_executable(illuminategfxd3d12)
  if (BUILD_WITH_TEST)
	target_sources(illuminategfxd3d12 PRIVATE "d3d12_test.cpp" "d3d12_test_code.cpp")
  endif()
else()
  add_library(illuminategfxd3d12)
endif()

if (BUILD_WITH_TEST)
  target_compile_definitions(illuminategfxd3d12 PRIVATE BUILD_WITH_TEST)
else()
  target_compile_definitions(illuminategfxd3d12 PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_link_libraries(illuminategfxd3d12
  PUBLIC
  illuminategfx
  illuminategfxwin32
  PRIVATE
  ${dxcapi_SOURCE_DIR}/lib/dxcompiler.lib
  dxguid.lib
  )

target_include_directories(illuminategfxd3d12
  PRIVATE
  "${d3dx12_SOURCE_DIR}/Libraries/D3DX12"
  "${d3d12memoryallocator_SOURCE_DIR}/src"
  "${dxcapi_SOURCE_DIR}/include"
  ".."
)

target_sources(illuminategfxd3d12 PRIVATE
  "d3d12_dxgi_core.cpp"
  "d3d12_device.cpp"
  "d3d12_util.cpp"
  "d3d12_command_allocator.cpp"
  "d3d12_command_list.cpp"
  "d3d12_command_queue.cpp"
  "d3d12_swapchain.cpp"
  "d3d12_shader_visible_descriptor_heap.cpp"
  "d3d12_descriptor_heap.cpp"
  "${d3d12memoryallocator_SOURCE_DIR}/src/D3D12MemAlloc.cpp"
  "d3d12_shader_compiler.cpp"
)

add_custom_command(TARGET illuminategfxd3d12 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dxcapi_SOURCE_DIR}/bin/dxcompiler.dll $<TARGET_FILE_DIR:illuminategfxd3d12>
  )
if (BUILD_WITH_TEST)
  add_custom_command(TARGET illuminategfxd3d12 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/../shader ${CMAKE_BINARY_DIR}/shader
	)
endif()
