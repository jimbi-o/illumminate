#include "dxc/Support/WinIncludes.h"
#include "dxc/Support/WinFunctions.h"
#include "dxc/dxcapi.h"
#include "minimal_for_cpp.h"
// wanted to move to upper directory, but could not remove errors compiling WinAdapter.h
namespace {
// https://simoncoenen.com/blog/programming/graphics/DxcRevised.html
IDxcUtils* CreateDxcUtils() {
  IDxcUtils* utils = nullptr;
  auto hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&utils));
  if (SUCCEEDED(hr)) return utils;
  logerror("CreateDxcUtils failed. {}", hr);
  return nullptr;
}
IDxcCompiler3* CreateDxcShaderCompiler() {
  IDxcCompiler3* compiler = nullptr;
  auto hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
  if (SUCCEEDED(hr)) return compiler;
  logerror("CreateDxcShaderCompiler failed. {}", hr);
  return nullptr;
}
void* CreateShaderResource(IDxcUtils* const utils, IDxcCompiler3* const compiler) {
  /*
  virtual HRESULT STDMETHODCALLTYPE Compile(
    _In_ const DxcBuffer *pSource,                // Source text to compile
    _In_opt_count_(argCount) LPCWSTR *pArguments, // Array of pointers to arguments
    _In_ UINT32 argCount,                         // Number of arguments
    _In_opt_ IDxcIncludeHandler *pIncludeHandler, // user-provided interface to handle #include directives (optional)
    _In_ REFIID riid, _Out_ LPVOID *ppResult      // IDxcResult: status, buffer, and errors
  ) = 0;
   */
  return nullptr;
}
}
#include "doctest/doctest.h"
TEST_CASE("compile shader using dxc") {
  auto utils = CreateDxcUtils();
  CHECK(utils);
  auto compiler = CreateDxcShaderCompiler();
  CHECK(compiler);
  auto shader_resource = CreateShaderResource(utils, compiler);
  CHECK(shader_resource);
}
