cmake_minimum_required(VERSION 3.15)
project(illuminategfx
  VERSION 1.0
  DESCRIPTION "renderer"
  LANGUAGES CXX
)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  if (NOT DEFINED BUILD_WITH_TEST)
	set(BUILD_WITH_TEST ON)
  endif()
  add_executable(illuminategfx)
  if (BUILD_WITH_TEST)
	target_sources(illuminategfx PRIVATE "gfx_test.cpp")
  endif()
else()
  add_library(illuminategfx)
endif()

if (NOT TARGET illuminatecore)
  add_subdirectory("../core" "${CMAKE_CURRENT_BINARY_DIR}/core")
endif()

target_compile_features(illuminategfx PUBLIC cxx_std_20)
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminategfx PRIVATE BUILD_WITH_TEST)
else()
  target_compile_definitions(illuminategfx PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_link_libraries(illuminategfx PUBLIC illuminatecore)

target_sources(illuminategfx PRIVATE
  "render_graph.cpp"
)
