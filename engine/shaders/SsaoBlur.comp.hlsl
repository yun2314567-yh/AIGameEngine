#include "Common.hlsli"

[[vk::binding(16, 0)]] Texture2D<float> SceneDepth : register(t0);
[[vk::binding(17, 0)]] Texture2D<float> RawOcclusion : register(t1);
[[vk::binding(48, 0)]] RWTexture2D<float> AmbientOcclusion : register(u0);

// Joint bilateral filtering removes hemisphere-sampling noise while retaining
// silhouettes and contact edges. Depth is compared in linear view space.
[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint width, height;
    AmbientOcclusion.GetDimensions(width, height);
    if (dispatchId.x >= width || dispatchId.y >= height) return;
    int2 pixel = int2(dispatchId.xy);
    float depth = SceneDepth.Load(int3(pixel, 0));
    if (depth >= 1.0 - Epsilon) {
        AmbientOcclusion[pixel] = 1.0;
        return;
    }
    float2 inverseSize = 1.0 / float2(width, height);
    float centerZ = ReconstructViewPosition((float2(pixel) + 0.5) * inverseSize, depth).z;
    float depthScale = max(0.02, abs(centerZ) * 0.01);
    float total = 0.0, weights = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            int2 neighbor = clamp(pixel + int2(x, y), int2(0, 0), int2(width, height) - 1);
            float neighborDepth = SceneDepth.Load(int3(neighbor, 0));
            if (neighborDepth >= 1.0 - Epsilon) continue;
            float neighborZ = ReconstructViewPosition((float2(neighbor) + 0.5) * inverseSize, neighborDepth).z;
            float weight = exp(-float(x * x + y * y) * 0.22 - abs(neighborZ - centerZ) / depthScale);
            total += RawOcclusion.Load(int3(neighbor, 0)) * weight;
            weights += weight;
        }
    }
    AmbientOcclusion[pixel] = total / max(weights, Epsilon);
}
