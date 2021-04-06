#include "fullscreen-triangle.hlsli"
#define TestPsRootsig ""
[RootSignature(TestPsRootsig)]
float4 main(FullscreenTriangleVSOutput input) : SV_TARGET0 {
	return float4(input.texcoord, 1.0f, 1.0f);
}
