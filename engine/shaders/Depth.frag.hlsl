#include "Common.hlsli"
#include "Material.hlsli"

struct DepthVertex {
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 TexCoord : TEXCOORD0;
};

void main(DepthVertex input) {
    ApplyAlphaMask(ReadBaseColor(input.TexCoord).a);
}
