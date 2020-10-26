cmake_minimum_required(VERSION 3.15)
project(illuminategfx
  VERSION 1.0
  DESCRIPTION "renderer"
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

add_subdirectory("../core" "${CMAKE_CURRENT_BINARY_DIR}/core")

target_compile_features(illuminategfx PUBLIC cxx_std_20)
if (NOT MSVC)
  target_compile_options(illuminategfx PRIVATE -msse4.2)
endif()
if ("${CMAKE_VS_PLATFORM_TOOLSET}" STREQUAL "ClangCL")
  target_compile_options(illuminategfx PRIVATE "/arch:AVX2")
endif()
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminategfx PRIVATE BUILD_WITH_TEST)
else()
  target_compile_definitions(illuminategfx PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_link_libraries(illuminategfx PRIVATE spdlog illuminatecore)

target_include_directories(illuminategfx
  PUBLIC
  "../../../include"
  PRIVATE
  "."
  "../"
  "../../include/illuminate"
  "../../include/illuminate/gfx"
  "${doctest_SOURCE_DIR}"
  "${spdlog_SOURCE_DIR}/include"
)

target_sources(illuminategfx PRIVATE
#  "renderer.cpp" # TODO remove file
)
