cbuffer ShadowCB : register(b0)
{
    float4x4 gWorldViewProj;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 Tex : TEXCOORD;
};

float4 VSMain(VSInput vin) : SV_POSITION
{
    return mul(float4(vin.Pos, 1.0f), gWorldViewProj);
}
