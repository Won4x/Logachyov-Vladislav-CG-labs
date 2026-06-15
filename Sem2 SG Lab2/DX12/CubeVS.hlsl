cbuffer ObjectCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;

    float3 gEyePosW;
    float pad0;

    float4 gLightDir;
    float4 gDiffuseColor;
    float4 gSpecularColor;
    float gShininess;
    float3 pad1;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float3 PosL : TEXCOORD2;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput vout;

    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gWorldViewProj);

    float3 normalW = mul(float4(vin.Normal, 0.0f), gWorld).xyz;
    vout.NormalW = normalize(normalW);
    vout.PosL = vin.Pos;

    return vout;
}
