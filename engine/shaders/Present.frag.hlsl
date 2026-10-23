[[vk::binding(16, 0)]] Texture2D<float4> DisplayColor : register(t0);
[[vk::binding(32, 0)]] SamplerState LinearClampSampler : register(s0);

float4 main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0 {
    return float4(DisplayColor.SampleLevel(LinearClampSampler, uv, 0.0).rgb, 1.0);
}
