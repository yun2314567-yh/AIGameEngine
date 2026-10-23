#ifndef TINYENGINE_MATERIAL_HLSLI
#define TINYENGINE_MATERIAL_HLSLI

[[vk::binding(2, 0)]] cbuffer MaterialConstants : register(b2) {
    float4 BaseColorFactor;
    float4 MaterialFactors;
    float4 EmissiveAlphaCutoff;
    uint4 TextureMaskAlphaMode;
};

[[vk::binding(16, 0)]] Texture2D<float4> BaseColorTexture : register(t0);
[[vk::binding(32, 0)]] SamplerState LinearWrapSampler : register(s0);

float4 ReadBaseColor(float2 uv) {
    float4 baseColor = BaseColorFactor;
    if ((TextureMaskAlphaMode.x & 1u) != 0u)
        baseColor *= BaseColorTexture.Sample(LinearWrapSampler, uv);
    return baseColor;
}

void ApplyAlphaMask(float alpha) {
    if (TextureMaskAlphaMode.y == 1u)
        clip(alpha - EmissiveAlphaCutoff.w);
}

#endif
