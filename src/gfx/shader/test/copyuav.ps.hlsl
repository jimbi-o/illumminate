#include "fullscreen-triangle.hlsli"
RWTexture2D<unorm float4> src : register(u0);
#define CopyFullscreenRootsig "DescriptorTable(UAV(u0), visibility=SHADER_VISIBILITY_PIXEL)"
[RootSignature(CopyFullscreenRootsig)]
float4 main(FullscreenTriangleVSOutput input) : SV_TARGET0 {
  float4 color = src.Load(int2(input.position.x, input.position.y));
  return color;
}
