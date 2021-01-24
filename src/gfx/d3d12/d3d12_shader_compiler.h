#ifndef ILLUMINATE_D3D12_SHADER_COMPILER_H__
#define ILLUMINATE_D3D12_SHADER_COMPILER_H__
#include "d3d12_header_common.h"
#include "dxc/dxcapi.h" // wanted to move this source code to upper directory, but could not remove errors compiling WinAdapter.h
namespace illuminate::gfx::d3d12 {
class ShaderCompiler {
 public:
  bool Init(D3d12Device* const device, std::pmr::memory_resource* memory_resource);
  void Term();
  IDxcResult* Compile(const char* filename, LPCWSTR target_profile, std::pmr::memory_resource* memory_resource);
  void ReleaseResult(IDxcResult* const);
 private:
  D3d12Device* device_ = nullptr;
  IDxcUtils* utils_ = nullptr;
  IDxcCompiler3* compiler_ = nullptr;
  std::pmr::unordered_set<IDxcResult*> results_;
};
}
#endif
