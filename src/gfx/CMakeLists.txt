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
)
FetchContent_MakeAvailable(doctest)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        39150eb8c7673f37d740d957acfcbff8c48ef9a6
  )
FetchContent_MakeAvailable(spdlog)
FetchContent_Declare(
  dxc
  GIT_REPOSITORY https://github.com/microsoft/DirectXShaderCompiler.git
  GIT_TAG        ac60ae1f4d7840678382f4959d4c04632fd35b5e
)
FetchContent_GetProperties(dxc)
if (NOT dxc_Populated)
  FetchContent_Populate(dxc)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "dxc build parameter")
  set(LLVM_APPEND_VC_REV ON CACHE INTERNAL "dxc build parameter")
  set(LLVM_DEFAULT_TARGET_TRIPLE dxil-ms-dx CACHE INTERNAL "dxc build parameter")
  set(LLVM_ENABLE_EH ON CACHE INTERNAL "dxc build parameter")
  set(LLVM_ENABLE_RTTI ON CACHE INTERNAL "dxc build parameter")
  set(LLVM_INCLUDE_DOCS OFF CACHE INTERNAL "dxc build parameter")
  set(LLVM_INCLUDE_EXAMPLES OFF CACHE INTERNAL "dxc build parameter")
  set(LLVM_INCLUDE_TESTS OFF CACHE INTERNAL "dxc build parameter")
  set(LLVM_OPTIMIZED_TABLEGEN OFF CACHE INTERNAL "dxc build parameter")
  set(LLVM_REQUIRES_EH ON CACHE INTERNAL "dxc build parameter")
  set(LLVM_REQUIRES_RTTI ON CACHE INTERNAL "dxc build parameter")
  set(LLVM_TARGETS_TO_BUILD None CACHE INTERNAL "dxc build parameter")
  set(LIBCLANG_BUILD_STATIC ON CACHE INTERNAL "dxc build parameter")
  set(CLANG_BUILD_EXAMPLES OFF CACHE INTERNAL "dxc build parameter")
  set(CLANG_CL OFF CACHE INTERNAL "dxc build parameter")
  set(CLANG_ENABLE_ARCMT OFF CACHE INTERNAL "dxc build parameter")
  set(CLANG_ENABLE_STATIC_ANALYZER OFF CACHE INTERNAL "dxc build parameter")
  set(CLANG_INCLUDE_TESTS OFF CACHE INTERNAL "dxc build parameter")
  set(HLSL_INCLUDE_TESTS OFF CACHE INTERNAL "dxc build parameter")
  set(ENABLE_SPIRV_CODEGEN ON CACHE INTERNAL "dxc build parameter")
  set(SPIRV_BUILD_TESTS OFF CACHE INTERNAL "dxc build parameter")
  set(DXC_BUILD_ARCH x64 CACHE INTERNAL "dxc build parameter")
  set(SPIRV_WERROR OFF CACHE INTERNAL "dxc build parameter")
  add_subdirectory(${dxc_SOURCE_DIR} ${dxc_BINARY_DIR})
endif()

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

target_link_libraries(illuminategfx PRIVATE spdlog illuminatecore dxcompiler)

target_include_directories(illuminategfx
  PUBLIC
  "../../../include"
  "${dxc_SOURCE_DIR}/include"
  PRIVATE
  "."
  "../"
  "../../include/illuminate"
  "../../include/illuminate/gfx"
  "${doctest_SOURCE_DIR}"
  "${spdlog_SOURCE_DIR}/include"
)

target_sources(illuminategfx PRIVATE
  "render_graph.cpp"
)
