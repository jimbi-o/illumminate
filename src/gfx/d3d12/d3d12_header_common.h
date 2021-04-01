#ifndef ILLUMINATE_HEADER_COMMON_H
#define ILLUMINATE_HEADER_COMMON_H
#include <tuple>
#include <unordered_map>
#include <vector>
#include <dxgi1_6.h>
#include <d3d12.h>
#include "gfx_def.h"
#include "illuminate/illuminate.h"
namespace illuminate::gfx::d3d12 {
using DxgiFactory = IDXGIFactory7;
using DxgiAdapter = IDXGIAdapter4;
using DxgiSwapchain = IDXGISwapChain4;
using D3d12Device = ID3D12Device6;
using D3d12CommandList = ID3D12GraphicsCommandList6;
using CommandQueueType = illuminate::gfx::CommandQueueType;
using BufferFormat = illuminate::gfx::BufferFormat;
using BufferSizeType = illuminate::gfx::BufferSizeType;
constexpr inline D3D12_COMMAND_LIST_TYPE ConvertToD3d12CommandQueueType(const CommandQueueType type) {
  switch (type) {
    case CommandQueueType::kGraphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case CommandQueueType::kCompute:  return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case CommandQueueType::kTransfer: return D3D12_COMMAND_LIST_TYPE_COPY;
  }
  return D3D12_COMMAND_LIST_TYPE_DIRECT;
}
constexpr inline DXGI_FORMAT GetDxgiFormat(const BufferFormat format) {
  switch(format) {
    case BufferFormat::kR8G8B8A8Unorm:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case BufferFormat::kD24S8:
      return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case BufferFormat::kD32Float:
      return DXGI_FORMAT_D32_FLOAT;
    case BufferFormat::kUnknown:
      return DXGI_FORMAT_UNKNOWN;
  }
  return DXGI_FORMAT_UNKNOWN;
}
}
#endif
