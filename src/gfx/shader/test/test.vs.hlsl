struct VSInput {
  float4 pos : POSITION;
};
float4 main(VSInput input) : SV_POSITION {
  return 1.0f;
}
