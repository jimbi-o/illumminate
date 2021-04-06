#include "fullscreen-triangle.hlsli"
#define CbvTestRootsig "DescriptorTable(CBV(b0), visibility=SHADER_VISIBILITY_PIXEL)"
struct CbvLocal {
	float4 color;
};
ConstantBuffer<CbvLocal> cbv_local : register(b0, space0);
[RootSignature(CbvTestRootsig)]
float4 main(FullscreenTriangleVSOutput input) : SV_TARGET0 {
	return saturate(cbv_local.color + float4(input.texcoord, 1.0f, 1.0f));
}
