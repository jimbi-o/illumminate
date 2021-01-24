#define TestRS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | DENY_VERTEX_SHADER_ROOT_ACCESS)"
[RootSignature(TestRS)]
float4 main() : SV_POSITION {
  return 1.0f;
}
