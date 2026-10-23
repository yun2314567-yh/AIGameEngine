struct FullscreenVertex {
    float4 Position : SV_Position;
    [[vk::location(0)]] float2 TexCoord : TEXCOORD0;
};

FullscreenVertex main(uint vertexId : SV_VertexID) {
    FullscreenVertex output;
    float2 uv = float2((vertexId << 1u) & 2u, vertexId & 2u);
    output.Position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.TexCoord = uv;
    return output;
}
