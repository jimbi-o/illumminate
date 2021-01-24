#include "d3d12_shader_compiler.h"
#include "d3d12_device.h"
#include "d3d12_dxgi_core.h"
#include "d3d12_minimal_for_cpp.h"
namespace illuminate::gfx::d3d12 {
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
  arguments.push_back(L"-Qstrip_rootsignature");
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
#ifdef BUILD_WITH_TEST
const uint32_t buffer_size_in_bytes = 32 * 1024;
std::byte buffer[buffer_size_in_bytes]{};
#endif
}
bool ShaderCompiler::Init(D3d12Device* const device, std::pmr::memory_resource* memory_resource) {
  device_ = device;
  utils_ = CreateDxcUtils();
  if (!utils_) return false;
  compiler_ = CreateDxcShaderCompiler();
  if (!compiler_) return false;
  return true;
}
void ShaderCompiler::Term() {
  for (auto& r : results_) {
    r->Release();
  }
  compiler_->Release();
  utils_->Release();
}
IDxcResult* ShaderCompiler::Compile(const char* filename, LPCWSTR target_profile, std::pmr::memory_resource* memory_resource) {
  auto buffer_size = MAX_PATH;
  auto abspath = reinterpret_cast<char*>(memory_resource->allocate(buffer_size, alignof(char)));
  auto path_len = GetAbsolutePath(filename, abspath, buffer_size);
  auto abspath_mb = reinterpret_cast<wchar_t*>(memory_resource->allocate(strlen(abspath) * 2, alignof(char)));
  ConvertToLPCWSTR(abspath, abspath_mb, path_len);
  auto ret = CreateShaderResource(utils_, compiler_, abspath_mb, target_profile, memory_resource);
  memory_resource->deallocate(abspath, buffer_size, alignof(char));
  memory_resource->deallocate(abspath_mb, strlen(abspath) * 2, alignof(char));
  return ret;
}
void ShaderCompiler::ReleaseResult(IDxcResult* const result) {
  results_.erase(result);
  result->Release();
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
    auto root_signature_blob = GetResultOutput<IDxcBlob>(shader_result, DXC_OUT_ROOT_SIGNATURE);
    CHECK(root_signature_blob);
    ID3D12RootSignature* root_signature = nullptr;
    auto hr = device.Get()->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
    CHECK(SUCCEEDED(hr));
    CHECK(root_signature);
    auto shader_object = GetResultOutput<IDxcBlob>(shader_result, DXC_OUT_OBJECT);
    CHECK(shader_object);
    struct {
      CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature;
      CD3DX12_PIPELINE_STATE_STREAM_VS vs;
    } desc_local {
      root_signature,
      D3D12_SHADER_BYTECODE{shader_object->GetBufferPointer(), shader_object->GetBufferSize()},
    };
    D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(desc_local), &desc_local};
    ID3D12PipelineState* pso = nullptr;
    hr = device.Get()->CreatePipelineState(&desc, IID_PPV_ARGS(&pso));
    // if crash, enable dev mode + ExperimentalShaderModels (https://github.com/microsoft/DirectXShaderCompiler/issues/2550)
    CHECK(SUCCEEDED(hr));
    CHECK(pso);
    pso->Release();
    shader_object->Release();
    root_signature->Release();
    root_signature_blob->Release();
  }
  device.Term();
  dxgi_core.Term();
  shader_result->Release();
}
TEST_CASE("ShaderCompiler class") {
  using namespace illuminate;
  using namespace illuminate::gfx;
  using namespace illuminate::gfx::d3d12;
  auto memory_resource = std::make_shared<PmrLinearAllocator>(buffer, buffer_size_in_bytes);
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  ShaderCompiler shader_compiler;
  CHECK(shader_compiler.Init(device.Get(), memory_resource.get()));
  auto result = shader_compiler.Compile("shader/test/test.vs.hlsl", L"vs_6_5", memory_resource.get());
  CHECK(result);
  shader_compiler.ReleaseResult(result);
  result = shader_compiler.Compile("shader/test/test.vs.hlsl", L"vs_6_5", memory_resource.get());
  CHECK(result);
  shader_compiler.Term();
  device.Term();
  dxgi_core.Term();
}
