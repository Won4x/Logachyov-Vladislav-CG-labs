cbuffer GeometryCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4 gDiffuseColor;
    float4 gSpecularColor;
    float4 gMaterialParams;
    float4 gTextureTransform;
    float4 gEyeDisplacement;
    float4 gTessellationParams;
};

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gRoughnessMap : register(t3);
Texture2D gMetallicMap : register(t4);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
};

struct GBufferOutput
{
    float4 AlbedoMetallic : SV_Target0;
    float4 NormalRoughness : SV_Target1;
    float4 PositionAO : SV_Target2;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    return (len2 < 1e-8f) ? float3(0.0f, 1.0f, 0.0f) : v * rsqrt(len2);
}

GBufferOutput PSMain(PSInput pin)
{
    GBufferOutput output;
    float4 texColor = gDiffuseMap.Sample(gSampler, pin.Tex);
    float3 diffuseLinear = pow(saturate(texColor.rgb), 2.2f);
    float3 normalW = SafeNormalize(pin.NormalW);
    float3 tangentW = SafeNormalize(pin.TangentW - normalW * dot(pin.TangentW, normalW));
    float3 bitangentW = SafeNormalize(cross(normalW, tangentW));
    float3 normalSample = gNormalMap.Sample(gSampler, pin.Tex).xyz * 2.0f - 1.0f;
    normalW = SafeNormalize(normalSample.x * tangentW + normalSample.y * bitangentW + normalSample.z * normalW);

    float roughness = clamp(gRoughnessMap.Sample(gSampler, pin.Tex).r * gMaterialParams.x, 0.04f, 1.0f);
    float metallic = saturate(gMetallicMap.Sample(gSampler, pin.Tex).r * gMaterialParams.y);
    float ao = saturate(gMaterialParams.z);

    output.AlbedoMetallic = float4(diffuseLinear * gDiffuseColor.rgb, metallic);
    output.NormalRoughness = float4(normalW * 0.5f + 0.5f, roughness);
    output.PositionAO = float4(pin.PosW, ao);

    return output;
}
