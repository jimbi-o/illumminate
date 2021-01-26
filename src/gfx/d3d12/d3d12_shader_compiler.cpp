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
IDxcIncludeHandler* CreateDxcIncludeHandler(IDxcUtils* utils) {
  IDxcIncludeHandler* include_handler = nullptr;
  auto hr = utils->CreateDefaultIncludeHandler(&include_handler);
  if (SUCCEEDED(hr)) return include_handler;
  logerror("CreateDefaultIncludeHandler failed. {}", hr);
  return nullptr;
}
IDxcCompiler3* CreateDxcShaderCompiler() {
  IDxcCompiler3* compiler = nullptr;
  auto hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));
  if (SUCCEEDED(hr)) return compiler;
  logerror("CreateDxcShaderCompiler failed. {}", hr);
  return nullptr;
}
IDxcResult* CreateShaderResource(IDxcUtils* const utils, IDxcIncludeHandler* const include_handler, IDxcCompiler3* const compiler, LPCWSTR filename, LPCWSTR target_profile, std::pmr::memory_resource* memory_resource) {
  IDxcBlobEncoding* blob = nullptr;
  auto hr = utils->LoadFile(filename, nullptr, &blob);
  if (FAILED(hr)) {
    logerror(L"LoadFile for CreateShaderResource failed. {} {}", filename, hr);
    return {};
  }
  std::pmr::vector<LPCWSTR> arguments{memory_resource};
  arguments.reserve(12);
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
  arguments.push_back(L"-I");
  {
    auto include_path = reinterpret_cast<wchar_t*>(memory_resource->allocate((wcslen(filename) + 1) * sizeof(wchar_t), alignof(wchar_t)));
    wcscpy(include_path, filename);
    auto dir_term_pos = wcsrchr(include_path, L'/');
    *dir_term_pos = L'\0';
    arguments.push_back(include_path);
  }
  IDxcResult* result = nullptr;
  DxcBuffer source{blob->GetBufferPointer(), blob->GetBufferSize(), DXC_CP_ACP};
  hr = compiler->Compile(&source, arguments.data(), arguments.size(), include_handler, IID_PPV_ARGS(&result));
  blob->Release();
  if (auto error = ShaderCompiler::GetResultOutput<IDxcBlobUtf8>(result, DXC_OUT_ERRORS); error) {
    logerror("dxc:{}", error->GetStringPointer());
    error->Release();
  }
  if (FAILED(hr)) {
    logerror(L"Compile for CreateShaderResource failed. {} {}", filename, hr);
    result->Release();
    return {};
  }
  return result;
}
#ifdef BUILD_WITH_TEST
const uint32_t buffer_size_in_bytes = 32 * 1024;
std::byte buffer[buffer_size_in_bytes]{};
#endif
}
bool ShaderCompiler::Init(D3d12Device* const device) {
  device_ = device;
  utils_ = CreateDxcUtils();
  if (!utils_) return false;
  include_handler_ = CreateDxcIncludeHandler(utils_);
  if (!include_handler_) return false;
  compiler_ = CreateDxcShaderCompiler();
  if (!compiler_) return false;
  return true;
}
void ShaderCompiler::Term() {
  compiler_->Release();
  include_handler_->Release();
  utils_->Release();
}
IDxcResult* ShaderCompiler::Compile(const LPCWSTR filename, const ShaderType target_profile, std::pmr::memory_resource* memory_resource) {
  IDxcResult* ret = nullptr;
  switch (target_profile) {
    case ShaderType::kPs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"ps_6_5", memory_resource); break;
    case ShaderType::kVs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"vs_6_5", memory_resource); break;
    case ShaderType::kGs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"gs_6_5", memory_resource); break;
    case ShaderType::kHs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"hs_6_5", memory_resource); break;
    case ShaderType::kDs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"ds_6_5", memory_resource); break;
    case ShaderType::kCs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"cs_6_5", memory_resource); break;
    case ShaderType::kLib: ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"lib_6_5", memory_resource); break;
    case ShaderType::kMs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"ms_6_5", memory_resource); break;
    case ShaderType::kAs:  ret = CreateShaderResource(utils_, include_handler_, compiler_, filename, L"as_6_5", memory_resource); break;
  }
  return ret;
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
  auto include_handler = CreateDxcIncludeHandler(utils);
  CHECK(include_handler);
  auto compiler = CreateDxcShaderCompiler();
  CHECK(compiler);
  auto shader_result = CreateShaderResource(utils, include_handler, compiler, L"shader/test/test.vs.hlsl", L"vs_6_5", memory_resource.get());
  CHECK(shader_result);
  DxgiCore dxgi_core;
  CHECK(dxgi_core.Init());
  Device device;
  CHECK(device.Init(dxgi_core.GetAdapter()));
  {
    auto root_signature_blob = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result, DXC_OUT_ROOT_SIGNATURE);
    CHECK(root_signature_blob);
    ID3D12RootSignature* root_signature = nullptr;
    auto hr = device.Get()->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature));
    CHECK(SUCCEEDED(hr));
    CHECK(root_signature);
    auto shader_object = ShaderCompiler::GetResultOutput<IDxcBlob>(shader_result, DXC_OUT_OBJECT);
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
  compiler->Release();
  utils->Release();
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
  CHECK(shader_compiler.Init(device.Get()));
  auto result = shader_compiler.Compile(L"shader/test/test.vs.hlsl", ShaderType::kVs, memory_resource.get());
  CHECK(result);
  result = shader_compiler.Compile(L"shader/test/test.vs.hlsl", ShaderType::kVs, memory_resource.get());
  CHECK(result);
  shader_compiler.Term();
  device.Term();
  dxgi_core.Term();
}
