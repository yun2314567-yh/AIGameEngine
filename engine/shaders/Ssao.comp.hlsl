#include "Common.hlsli"

[[vk::binding(1, 0)]] cbuffer SsaoConstants : register(b1) {
    float4 RadiusBiasPower;
    uint4 SampleCountFrame;
    float4 Kernel[64];
};
[[vk::binding(16, 0)]] Texture2D<float> SceneDepth : register(t0);
[[vk::binding(48, 0)]] RWTexture2D<float> AmbientOcclusion : register(u0);
[[vk::binding(32, 0)]] SamplerState PointClampSampler : register(s0);

float3 LoadPosition(int2 pixel, uint2 dimensions) {
    int2 clampedPixel = clamp(pixel, int2(0, 0), int2(dimensions) - 1);
    float2 uv = (float2(clampedPixel) + 0.5) / float2(dimensions);
    return ReconstructViewPosition(uv, SceneDepth.Load(int3(clampedPixel, 0)));
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint width, height;
    AmbientOcclusion.GetDimensions(width, height);
    if (dispatchId.x >= width || dispatchId.y >= height)
        return;
    uint2 pixel = dispatchId.xy;
    float depth = SceneDepth.Load(int3(pixel, 0));
    if (depth >= 1.0 - Epsilon) {
        AmbientOcclusion[pixel] = 1.0;
        return;
    }

    uint2 dimensions = uint2(width, height);
    float3 position = LoadPosition(int2(pixel), dimensions);
    float3 left = position - LoadPosition(int2(pixel) + int2(-1, 0), dimensions);
    float3 right = LoadPosition(int2(pixel) + int2(1, 0), dimensions) - position;
    float3 up = position - LoadPosition(int2(pixel) + int2(0, -1), dimensions);
    float3 down = LoadPosition(int2(pixel) + int2(0, 1), dimensions) - position;
    float3 derivativeX = abs(left.z) < abs(right.z) ? left : right;
    float3 derivativeY = abs(up.z) < abs(down.z) ? up : down;
    float3 normal = SafeNormalize(cross(derivativeY, derivativeX), float3(0.0, 0.0, 1.0));
    if (dot(normal, -position) < 0.0)
        normal = -normal;

    float randomAngle = frac(52.9829189 * frac(dot(float2(pixel), float2(0.06711056, 0.00583715))
        + float(SampleCountFrame.y % 64u) * 0.0061)) * 2.0 * Pi;
    float3 randomVector = float3(cos(randomAngle), sin(randomAngle), 0.0);
    float3 tangent = randomVector - normal * dot(randomVector, normal);
    float3 fallbackAxis = abs(normal.z) < 0.99 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
    tangent = SafeNormalize(tangent, normalize(cross(fallbackAxis, normal)));
    float3 bitangent = cross(normal, tangent);
    uint count = clamp(SampleCountFrame.x, 1u, 64u);
    float radius = max(RadiusBiasPower.x, Epsilon);
    float occlusion = 0.0;
    float validSamples = 0.0;
    for (uint sampleIndex = 0u; sampleIndex < count; ++sampleIndex) {
        float3 kernel = Kernel[sampleIndex].xyz;
        float3 samplePosition = position + (tangent * kernel.x + bitangent * kernel.y + normal * kernel.z) * radius;
        float4 projected = mul(Projection, float4(samplePosition, 1.0));
        if (projected.w <= Epsilon)
            continue;
        float3 ndc = projected.xyz / projected.w;
        float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if (any(uv < 0.0) || any(uv > 1.0) || ndc.z <= 0.0 || ndc.z >= 1.0)
            continue;
        float sampledDepth = SceneDepth.SampleLevel(PointClampSampler, uv, 0.0);
        validSamples += 1.0;
        if (sampledDepth >= 1.0 - Epsilon)
            continue;
        float3 surface = ReconstructViewPosition(uv, sampledDepth);
        float rangeWeight = smoothstep(0.0, 1.0, radius / max(abs(position.z - surface.z), Epsilon));
        occlusion += (surface.z >= samplePosition.z + max(RadiusBiasPower.y, 0.0) ? 1.0 : 0.0) * rangeWeight;
    }
    float ao = 1.0 - occlusion / max(validSamples, 1.0);
    AmbientOcclusion[pixel] = pow(saturate(ao), max(RadiusBiasPower.z, Epsilon));
}
