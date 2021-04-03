#ifndef ILLUMINATE_D3D12_SHADER_COMPILER_H__
#define ILLUMINATE_D3D12_SHADER_COMPILER_H__
#include "d3d12_header_common.h"
#include "dxc/dxcapi.h" // wanted to move this source code to upper directory, but could not remove errors compiling WinAdapter.h
namespace illuminate::gfx::d3d12 {
class ShaderCompiler {
 public:
  template <typename T>
  static std::pair<T*, IDxcBlobUtf16*> GetResultOutputWithOutputName(IDxcResult* result, const DXC_OUT_KIND kind) {
    if (!result->HasOutput(kind)) return {};
    T* output = nullptr;
    IDxcBlobUtf16* output_text = nullptr;
    auto hr = result->GetOutput(kind, IID_PPV_ARGS(&output), &output_text);
    return {output, output_text};
  }
  template <typename T>
  static T* GetResultOutput(IDxcResult* result, const DXC_OUT_KIND kind) {
    auto [output, output_name] = GetResultOutputWithOutputName<T>(result, kind);
    if (output_name) output_name->Release();
    return output;
  }
  bool Init(D3d12Device* const device);
  void Term();
  IDxcResult* Compile(LPCWSTR filename, const ShaderType, std::pmr::memory_resource* memory_resource);
  std::tuple<ID3D12RootSignature*, ID3D12PipelineState*> CreateVsPsPipelineStateObject(LPCWSTR vs, LPCWSTR ps_with_rootsig, const DXGI_FORMAT* output_dxgi_format, const uint32_t output_dxgi_format_num, illuminate::core::EnableDisable enable_depth_stencil, std::pmr::memory_resource* memory_resource_work);
 private:
  D3d12Device* device_ = nullptr;
  IDxcUtils* utils_ = nullptr;
  IDxcIncludeHandler* include_handler_ = nullptr;
  IDxcCompiler3* compiler_ = nullptr;
};
}
#endif
