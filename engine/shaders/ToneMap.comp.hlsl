#include "Common.hlsli"

[[vk::binding(16, 0)]] Texture2D<float4> HdrColor : register(t0);
[[vk::binding(48, 0)]] RWTexture2D<float4> DisplayColor : register(u0);

float3 ToneMap(float3 hdr) {
    float3 color = max(hdr, 0.0) * max(CameraPositionExposure.w, 0.0);
    if (FeatureParameters.w == 0u) {
        float3 numerator = color * (2.51 * color + 0.03);
        float3 denominator = color * (2.43 * color + 0.59) + 0.14;
        return saturate(numerator / max(denominator, Epsilon));
    }
    if (FeatureParameters.w == 1u)
        return color / (1.0 + color);
    return saturate(color);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint width, height;
    DisplayColor.GetDimensions(width, height);
    if (dispatchId.x >= width || dispatchId.y >= height)
        return;
    DisplayColor[dispatchId.xy] = float4(ToneMap(HdrColor.Load(int3(dispatchId.xy, 0)).rgb), 1.0);
}
