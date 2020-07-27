#ifndef __ILLUMINATE_HEADER_COMMON_H__
#define __ILLUMINATE_HEADER_COMMON_H__
#include <dxgi1_6.h>
#include <d3d12.h>
#include "d3d12_util.h"
namespace illuminate::gfx::d3d12 {
using DxgiFactory = IDXGIFactory7;
using DxgiAdapter = IDXGIAdapter4;
using D3d12Device = ID3D12Device;
}
#endif
