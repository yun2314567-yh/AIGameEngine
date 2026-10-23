#ifndef TINYENGINE_GEOMETRY_HLSLI
#define TINYENGINE_GEOMETRY_HLSLI
#include "Common.hlsli"

[[vk::binding(1, 0)]] cbuffer ObjectConstants : register(b1) {
    column_major float4x4 Model;
    column_major float4x4 NormalMatrix;
};

struct MeshVertex {
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float3 Normal : NORMAL;
    [[vk::location(2)]] float4 Tangent : TANGENT;
    [[vk::location(3)]] float2 TexCoord : TEXCOORD0;
};

struct SurfaceVertex {
    float4 Position : SV_Position;
    [[vk::location(0)]] float3 WorldPosition : TEXCOORD0;
    [[vk::location(1)]] float3 Normal : TEXCOORD1;
    [[vk::location(2)]] float4 Tangent : TEXCOORD2;
    [[vk::location(3)]] float2 TexCoord : TEXCOORD3;
};

struct DepthMeshVertex {
    [[vk::location(0)]] float3 Position : POSITION;
    [[vk::location(1)]] float2 TexCoord : TEXCOORD0;
};

struct DepthVertex {
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 TexCoord : TEXCOORD0;
};

#endif
