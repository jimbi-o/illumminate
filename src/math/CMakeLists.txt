cmake_minimum_required(VERSION 3.15)
project(illuminatemath
  VERSION 1.0
  DESCRIPTION "math library for graphics"
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
  target_compile_options(illuminatemath PRIVATE -msse4.2)
endif()
if ("${CMAKE_VS_PLATFORM_TOOLSET}" STREQUAL "ClangCL")
  target_compile_options(illuminatemath PRIVATE "/arch:AVX2")
endif()
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminatemath PRIVATE BUILD_WITH_TEST)
  target_link_libraries(illuminatemath PRIVATE doctest::doctest)
else()
  target_compile_definitions(illuminatemath PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_include_directories(illuminatemath
  PUBLIC
  "../../include"
  PRIVATE
  "."
  "../../include/illuminate/math"
)
