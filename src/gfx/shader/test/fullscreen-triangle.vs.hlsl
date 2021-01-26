#include "fullscreen-triangle.hlsli"
FullscreenTriangleVSOutput main(uint id : SV_VERTEXID) {
  // https://www.reddit.com/r/gamedev/comments/2j17wk/a_slightly_faster_bufferless_vertex_shader_trick/
  FullscreenTriangleVSOutput output;
  output.texcoord.x = (id == 2) ?  2.0 :  0.0;
  output.texcoord.y = (id == 1) ?  2.0 :  0.0;
  output.position = float4(output.texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 1.0, 1.0);
  return output;
}
