cmake_minimum_required(VERSION 3.15)
project(illuminategfxwin32
  VERSION 1.0
  DESCRIPTION "win32 library for graphics"
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
  add_executable(illuminategfxwin32)
else()
  add_library(illuminategfxwin32)
endif()

target_compile_features(illuminategfxwin32 PUBLIC cxx_std_17)
if (NOT MSVC)
  target_compile_options(illuminategfxwin32 PRIVATE -msse4.2)
endif()
if (BUILD_WITH_TEST)
  target_compile_definitions(illuminategfxwin32 PRIVATE BUILD_WITH_TEST)
  target_link_libraries(illuminategfxwin32 PRIVATE doctest::doctest)
else()
  target_compile_definitions(illuminategfxwin32 PRIVATE DOCTEST_CONFIG_DISABLE)
endif()

target_link_libraries(illuminategfxwin32 PRIVATE spdlog)

target_include_directories(illuminategfxwin32
  PUBLIC
  "../../../include"
  PRIVATE
  "."
  "../../"
  "../../../include/illuminate/gfx/win32"
)

target_sources(illuminategfxwin32 PRIVATE
  "win32_window.cpp"
  "win32_handle.cpp"
)
if (BUILD_WITH_TEST)
  target_sources(illuminategfxwin32 PRIVATE
	"win32_test.cpp"
  )
endif()
