#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_minimal_for_cpp.h"
#include "dxc/dxcapi.h" // wanted to move this source code to upper directory, but could not remove errors compiling WinAdapter.h
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
template <typename T>
std::pair<T*, IDxcBlobUtf16*> GetResultOutputWithOutputName(IDxcResult* result, const DXC_OUT_KIND kind) {
  if (!result->HasOutput(kind)) return {};
  T* output = nullptr;
  IDxcBlobUtf16* output_text = nullptr;
  auto hr = result->GetOutput(kind, IID_PPV_ARGS(&output), &output_text);
  return {output, output_text};
}
template <typename T>
T* GetResultOutput(IDxcResult* result, const DXC_OUT_KIND kind) {
  auto [output, output_name] = GetResultOutputWithOutputName<T>(result, kind);
  if (output_name) output_name->Release();
  return output;
}
IDxcResult* CreateShaderResource(IDxcUtils* const utils, IDxcCompiler3* const compiler, LPCWSTR filepath_absolute, LPCWSTR target_profile, std::pmr::memory_resource* memory_resource) {
  IDxcBlobEncoding* blob = nullptr;
  auto hr = utils->LoadFile(filepath_absolute, nullptr, &blob);
  if (FAILED(hr)) {
    logerror(L"LoadFile for CreateShaderResource failed. {} {}", filepath_absolute, hr);
    return {};
  }
  std::pmr::vector<LPCWSTR> arguments{memory_resource};
  arguments.push_back(L"-E");
  arguments.push_back(L"main");
  arguments.push_back(L"-T");
  arguments.push_back(target_profile);
  arguments.push_back(DXC_ARG_DEBUG);
  arguments.push_back(DXC_ARG_PACK_MATRIX_ROW_MAJOR);
  arguments.push_back(DXC_ARG_WARNINGS_ARE_ERRORS);
  arguments.push_back(L"-Qstrip_debug");
  arguments.push_back(L"-Qstrip_reflect");
  // arguments.push_back(L"-Qstrip_rootsignature");
  IDxcResult* result = nullptr;
  DxcBuffer source{blob->GetBufferPointer(), blob->GetBufferSize(), DXC_CP_ACP};
  hr = compiler->Compile(&source, arguments.data(), arguments.size(), nullptr, IID_PPV_ARGS(&result));
  blob->Release();
  if (auto error = GetResultOutput<IDxcBlobUtf8>(result, DXC_OUT_ERRORS); error) {
    logerror("dxc:{}", error->GetStringPointer());
    error->Release();
  }
  if (FAILED(hr)) {
    logerror(L"Compile for CreateShaderResource failed. {} {}", filepath_absolute, hr);
    result->Release();
    return {};
  }
  return result;
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
#include "d3dx12.h"
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
  auto path_len = GetAbsolutePath("shader/test/test.vs.hlsl", abspath, buffer_size_in_bytes);
  CHECK(path_len == strlen(abspath));
  auto abspath_mb = reinterpret_cast<wchar_t*>(abspath + path_len);
  path_len = ConvertToLPCWSTR(abspath, abspath_mb, path_len);
  CHECK(path_len);
  auto shader_result = CreateShaderResource(utils, compiler, abspath_mb, L"vs_6_5", memory_resource.get());
  CHECK(shader_result);
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  {
    auto shader_object = GetResultOutput<IDxcBlob>(shader_result, DXC_OUT_OBJECT);
    CHECK(shader_object);
    struct PipilineStateDescLocal {
      CD3DX12_PIPELINE_STATE_STREAM_VS vs;
    };
    PipilineStateDescLocal desc_local{(D3D12_SHADER_BYTECODE{shader_object->GetBufferPointer(), shader_object->GetBufferSize()})};
    D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(desc_local), &desc_local};
    ID3D12PipelineState* pso = nullptr;
    auto hr = device.Get()->CreatePipelineState(&desc, IID_PPV_ARGS(&pso));
    // if crash, enable dev mode + ExperimentalShaderModels (https://github.com/microsoft/DirectXShaderCompiler/issues/2550)
    CHECK(SUCCEEDED(hr));
    CHECK(pso);
    pso->Release();
    shader_object->Release();
  }
  device.Term();
  dxgi_core.Term();
  shader_result->Release();
}
