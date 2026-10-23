#ifndef TINYENGINE_COMMON_HLSLI
#define TINYENGINE_COMMON_HLSLI

static const float Pi = 3.14159265358979323846;
static const float Epsilon = 1.0e-5;

struct LightConstants {
    float4 PositionRange;
    float4 ColorIntensity;
    float4 DirectionOuterCone;
    float4 Parameters;
};

[[vk::binding(0, 0)]] cbuffer FrameConstants : register(b0) {
    column_major float4x4 ViewProjection;
    column_major float4x4 View;
    column_major float4x4 Projection;
    column_major float4x4 InverseProjection;
    column_major float4x4 LightViewProjection;
    float4 CameraPositionExposure;
    float4 LightDirectionIntensity;
    float4 LightColor;
    float4 ViewportSizeInverse;
    float4 ShadowParameters;
    float4 EnvironmentParameters;
    uint4 FeatureParameters;
    uint4 AdditionalLightCount;
    LightConstants AdditionalLights[32];
};

float3 SafeNormalize(float3 value, float3 fallback) {
    float lengthSquared = dot(value, value);
    return lengthSquared > Epsilon * Epsilon ? value * rsqrt(lengthSquared) : fallback;
}

float3 RotateEnvironment(float3 direction) {
    float sine, cosine;
    sincos(EnvironmentParameters.y, sine, cosine);
    return float3(cosine * direction.x + sine * direction.z, direction.y,
                  -sine * direction.x + cosine * direction.z);
}

float3 ReconstructViewPosition(float2 uv, float depth) {
    float4 position = mul(InverseProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
    return position.xyz / max(abs(position.w), Epsilon) * (position.w < 0.0 ? -1.0 : 1.0);
}

#endif
