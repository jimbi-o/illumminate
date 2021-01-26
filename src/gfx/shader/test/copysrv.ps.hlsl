#include "fullscreen-triangle.hlsli"
Texture2D src : register(t0);
#define CopyFullscreenRootsig "DescriptorTable(SRV(t0), visibility=SHADER_VISIBILITY_PIXEL)"
[RootSignature(CopyFullscreenRootsig)]
float4 main(FullscreenTriangleVSOutput input) : SV_TARGET0 {
  float4 color = src.Load(int3(input.position.x, input.position.y, 0));
  return color;
}
