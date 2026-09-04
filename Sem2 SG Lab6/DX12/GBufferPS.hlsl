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
    float4 AlbedoSpec : SV_Target0;
    float4 NormalShininess : SV_Target1;
    float4 Position : SV_Target2;
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
    float3 normalW = SafeNormalize(pin.NormalW);
    float3 tangentW = SafeNormalize(pin.TangentW - normalW * dot(pin.TangentW, normalW));
    float3 bitangentW = SafeNormalize(cross(normalW, tangentW));
    float3 normalSample = gNormalMap.Sample(gSampler, pin.Tex).xyz * 2.0f - 1.0f;
    normalW = SafeNormalize(normalSample.x * tangentW + normalSample.y * bitangentW + normalSample.z * normalW);

    output.AlbedoSpec = float4(texColor.rgb * gDiffuseColor.rgb, saturate(max(max(gSpecularColor.r, gSpecularColor.g), gSpecularColor.b)));
    output.NormalShininess = float4(normalW * 0.5f + 0.5f, saturate(gMaterialParams.x / 256.0f));
    output.Position = float4(pin.PosW, 1.0f);

    return output;
}
