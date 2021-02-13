cmake_minimum_required(VERSION 3.15)
project(illuminatemath
  VERSION 1.0
  DESCRIPTION "math library for graphics"
  LANGUAGES CXX
)
add_subdirectory("../core" "${CMAKE_CURRENT_BINARY_DIR}/core")

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  if (NOT DEFINED BUILD_WITH_TEST)
	set(BUILD_WITH_TEST ON)
  endif()
  add_executable(illuminatemath)
  if (BUILD_WITH_TEST)
	target_sources(illuminatemath PRIVATE "vec_test.cpp")
  endif()
else()
  add_library(illuminatemath)
endif()

target_compile_features(illuminatemath PUBLIC cxx_std_20)
if (NOT MSVC)
  target_compile_options(illuminatemath PUBLIC -msse4.2)
endif()
if ("${CMAKE_VS_PLATFORM_TOOLSET}" STREQUAL "ClangCL")
  target_compile_options(illuminatemath PRIVATE "/arch:AVX2")
endif()
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminatemath PRIVATE BUILD_WITH_TEST)
else()
  target_compile_definitions(illuminatemath PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_include_directories(illuminatemath PRIVATE ".")
