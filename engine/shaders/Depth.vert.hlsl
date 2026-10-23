#include "Geometry.hlsli"

DepthVertex main(DepthMeshVertex input) {
    DepthVertex output;
    output.Position = mul(ViewProjection, mul(Model, float4(input.Position, 1.0)));
    output.TexCoord = input.TexCoord;
    return output;
}
