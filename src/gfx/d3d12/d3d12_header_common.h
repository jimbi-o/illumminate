#ifndef __ILLUMINATE_HEADER_COMMON_H__
#define __ILLUMINATE_HEADER_COMMON_H__
#include <tuple>
#include <unordered_map>
#include <vector>
#include <dxgi1_6.h>
#include <d3d12.h>
#include "gfx_def.h"
namespace illuminate::gfx::d3d12 {
using DxgiFactory = IDXGIFactory7;
using DxgiAdapter = IDXGIAdapter4;
using DxgiSwapchain = IDXGISwapChain4;
using D3d12Device = ID3D12Device6;
using D3d12CommandList = ID3D12GraphicsCommandList5;
using CommandQueueType = illuminate::gfx::CommandQueueType;
using BufferFormat = illuminate::gfx::BufferFormat;
using BufferSizeType = illuminate::gfx::BufferSizeType;
constexpr inline D3D12_COMMAND_LIST_TYPE ConvertToD3d12CommandQueueType(const CommandQueueType type) {
  switch (type) {
    case CommandQueueType::kGraphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case CommandQueueType::kCompute:  return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case CommandQueueType::kTransfer: return D3D12_COMMAND_LIST_TYPE_COPY;
  }
}
constexpr inline DXGI_FORMAT GetDxgiFormat(const BufferFormat format) {
  switch(format) {
    case BufferFormat::kUseSwapchainFormat:
      [[fallthrough]];
    case BufferFormat::kR8G8B8A8_Unorm:
      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case BufferFormat::kD24S8:
      return DXGI_FORMAT_D24_UNORM_S8_UINT;
  }
  return DXGI_FORMAT_UNKNOWN;
}
constexpr inline DXGI_FORMAT GetDxgiFormat(const BufferFormat format, const DXGI_FORMAT swapchain_format) {
  if (format == BufferFormat::kUseSwapchainFormat) return swapchain_format;
  return GetDxgiFormat(format);
}
}
#endif
