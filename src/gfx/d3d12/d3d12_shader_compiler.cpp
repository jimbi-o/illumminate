#include "dxc/Support/WinIncludes.h"
#include "dxc/Support/WinFunctions.h"
#include "dxc/dxcapi.h"
#include "d3d12_minimal_for_cpp.h"
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
IDxcBlob* CreateShaderResource(IDxcUtils* const utils, IDxcCompiler3* const compiler, LPCWSTR filepath_absolute, std::pmr::memory_resource* memory_resource) {
  IDxcBlobEncoding* blob = nullptr;
  auto hr = utils->LoadFile(filepath_absolute, nullptr, &blob);
  if (FAILED(hr)) {
    logerror(L"LoadFile for CreateShaderResource failed. {} {}", filepath_absolute, hr);
    return nullptr;
  }
  std::pmr::vector<LPCWSTR> arguments{memory_resource};
  IDxcResult* result = nullptr;
  DxcBuffer source{blob->GetBufferPointer(), blob->GetBufferSize(), DXC_CP_ACP};
  hr = compiler->Compile(&source, arguments.data(), arguments.size(), nullptr, IID_PPV_ARGS(&result));
  if (FAILED(hr) || result->HasOutput(DXC_OUT_ERRORS)) {
    logerror(L"Compile for CreateShaderResource failed. {} {}", filepath_absolute, hr);
    IDxcBlobUtf8* error = nullptr;
    hr = result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&error), nullptr);
    if (SUCCEEDED(hr) && error) {
      logerror("{}", error->GetStringPointer());
    }
    return nullptr;
  }
  if (!result->HasOutput(DXC_OUT_OBJECT)) {
    logerror(L"Missing object in shader, {}", filepath_absolute);
    return nullptr;
  }
  IDxcBlob* shaderobj = nullptr;
  return shaderobj;
}
const uint32_t buffer_size_in_bytes = 32 * 1024;
std::byte buffer[buffer_size_in_bytes]{};
uint32_t GetAbsolutePath(const char* filename, char* dst, const uint32_t dst_size) {
  auto len = GetModuleFileName(nullptr, dst, dst_size);
  if (len == 0) {
    logerror("GetModuleFileName failed.");
    return 0;
  }
  auto pos = strrchr(dst, '\\');
  pos++;
  strcpy(pos, filename);
  len = strlen(filename);
  return pos - dst + len;
}
uint32_t ConvertToLPCWSTR(const char* text, wchar_t* dst, const uint32_t text_len) {
  auto len = mbstowcs(dst, text, text_len);
  return len;
}
}
#include "doctest/doctest.h"
TEST_CASE("compile shader using dxc") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  auto utils = CreateDxcUtils();
  CHECK(utils);
  auto compiler = CreateDxcShaderCompiler();
  CHECK(compiler);
  auto abspath = reinterpret_cast<char*>(buffer);
  auto path_len = GetAbsolutePath("test/test.vs.hlsl", abspath, buffer_size_in_bytes);
  CHECK(path_len == strlen(abspath));
  auto abspath_mb = reinterpret_cast<wchar_t*>(abspath + path_len);
  path_len = ConvertToLPCWSTR(abspath, abspath_mb, path_len);
  CHECK(path_len);
  auto shader_resource = CreateShaderResource(utils, compiler, abspath_mb, memory_resource.get());
  CHECK(shader_resource);
}
