cmake_minimum_required(VERSION 3.15)
project(illuminategfxwin32
  VERSION 1.0
  DESCRIPTION "win32 library for graphics"
  LANGUAGES CXX
)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  if (NOT DEFINED BUILD_WITH_TEST)
	set(BUILD_WITH_TEST ON)
  endif()
  add_executable(illuminategfxwin32)
  if (BUILD_WITH_TEST)
	target_sources(illuminategfxwin32 PRIVATE "win32_test.cpp")
  endif()
else()
  add_library(illuminategfxwin32)
endif()

if (NOT TARGET illuminatecore)
  add_subdirectory("../../core" "${CMAKE_CURRENT_BINARY_DIR}/core")
endif()

target_compile_features(illuminategfxwin32 PUBLIC cxx_std_20)
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminategfxwin32 PRIVATE BUILD_WITH_TEST)
else()
  target_compile_definitions(illuminategfxwin32 PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_link_libraries(illuminategfxwin32 PUBLIC illuminatecore)

target_sources(illuminategfxwin32 PRIVATE
  "win32_window.cpp"
  "win32_handle.cpp"
)
