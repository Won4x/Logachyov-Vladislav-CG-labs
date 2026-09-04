cbuffer GeometryCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4 gDiffuseColor;
    float4 gSpecularColor;
    float4 gMaterialParams;
    float4 gTextureTransform; // xy = scale, zw = offset.
    float4 gEyeDisplacement;  // xyz = eye position, w = displacement scale.
    float4 gTessellationParams; // x=max, y=min, z=near distance, w=far distance.
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
    float3 LocalPos : TEXCOORD3;
    float3 TangentW : TEXCOORD4;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput vout;
    float4 posW = mul(float4(vin.Pos, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = normalize(mul(float4(vin.Normal, 0.0f), gWorld).xyz);
    vout.LocalPos = vin.Pos;
    vout.TangentW = normalize(mul(float4(vin.Tangent, 0.0f), gWorld).xyz);
    vout.Tex = vin.Tex * gTextureTransform.xy + gTextureTransform.zw;
    return vout;
}
