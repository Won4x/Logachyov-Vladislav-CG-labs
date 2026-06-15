cbuffer GeometryCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4 gDiffuseColor;
    float4 gSpecularColor;
    float gShininess;
    float gTextureScaleX;
    float gTextureScaleY;
    float gTextureOffsetX;
    float gTextureOffsetY;
    float3 gPad0;
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput vout;
    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.PosH = mul(float4(vin.Pos, 1.0f), gWorldViewProj);
    vout.NormalW = normalize(mul(float4(vin.Normal, 0.0f), gWorld).xyz);
    vout.Tex = vin.Tex * float2(gTextureScaleX, gTextureScaleY) + float2(gTextureOffsetX, gTextureOffsetY);
    return vout;
}
