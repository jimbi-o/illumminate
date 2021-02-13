cmake_minimum_required(VERSION 3.15)
project(illuminatecore
  VERSION 1.0
  DESCRIPTION "renderer"
  LANGUAGES CXX
)

include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/onqtam/doctest.git
  GIT_TAG        f0ab4e793fa863d4a5929ff263ddd3bebec8767e
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(doctest)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        8fbc853b0dde6ba5f739cdacd0f3b538876f5695
  GIT_SHALLOW    OFF
)
FetchContent_MakeAvailable(spdlog)

if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
  if (NOT DEFINED BUILD_WITH_TEST)
	set(BUILD_WITH_TEST ON)
  endif()
  add_executable(illuminatecore)
  if (BUILD_WITH_TEST)
	target_sources(illuminatecore
	  PRIVATE
	  "core_test.cpp"
	  "memory.cpp"
	  )
  endif()
else()
  add_library(illuminatecore)
endif()

target_compile_features(illuminatecore PUBLIC cxx_std_20)
if (WIN32)
  target_compile_options(illuminatecore PUBLIC "/arch:AVX2")
else()
  target_compile_options(illuminatecore PUBLIC -msse4.2)
endif()
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminatecore PRIVATE BUILD_WITH_TEST)
else()
  target_compile_definitions(illuminatecore PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_link_libraries(illuminatecore PUBLIC spdlog::spdlog)

target_include_directories(illuminatecore
  PUBLIC
  "../../include"
  "${doctest_SOURCE_DIR}"
  "${spdlog_SOURCE_DIR}/include"
  PRIVATE
  "."
)

target_sources(illuminatecore PRIVATE
  "strid.cpp"
  "util.cpp"
)
