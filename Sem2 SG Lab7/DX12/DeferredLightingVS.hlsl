struct VSOutput
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    float2 pos = float2((vertexId == 2) ? 3.0f : -1.0f, (vertexId == 1) ? 3.0f : -1.0f);
    output.PosH = float4(pos, 0.0f, 1.0f);
    output.Tex = float2(0.5f * (pos.x + 1.0f), 0.5f * (1.0f - pos.y));
    return output;
}
