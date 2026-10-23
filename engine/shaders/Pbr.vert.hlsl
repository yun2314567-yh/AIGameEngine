#include "Geometry.hlsli"

SurfaceVertex main(MeshVertex input) {
    SurfaceVertex output;
    float4 worldPosition = mul(Model, float4(input.Position, 1.0));
    output.Position = mul(ViewProjection, worldPosition);
    output.WorldPosition = worldPosition.xyz;
    output.Normal = mul((float3x3)NormalMatrix, input.Normal);
    output.Tangent = float4(mul((float3x3)Model, input.Tangent.xyz), input.Tangent.w * sign(determinant((float3x3)Model)));
    output.TexCoord = input.TexCoord;
    return output;
}
