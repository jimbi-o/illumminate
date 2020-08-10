#ifndef __ILLUMINATE_HEADER_COMMON_H__
#define __ILLUMINATE_HEADER_COMMON_H__
#include <tuple>
#include <unordered_map>
#include <vector>
#include <dxgi1_6.h>
#include <d3d12.h>
#include "d3d12_util.h"
#include "gfx_def.h"
namespace illuminate::gfx::d3d12 {
using DxgiFactory = IDXGIFactory7;
using DxgiAdapter = IDXGIAdapter4;
using D3d12Device = ID3D12Device;
using CommandListType = illuminate::gfx::CommandListType;
constexpr inline D3D12_COMMAND_LIST_TYPE ConvertToD3d12CommandListType(const CommandListType type) {
  switch (type) {
    case CommandListType::kGraphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case CommandListType::kCompute:  return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case CommandListType::kTransfer: return D3D12_COMMAND_LIST_TYPE_COPY;
  }
}
}
#endif
