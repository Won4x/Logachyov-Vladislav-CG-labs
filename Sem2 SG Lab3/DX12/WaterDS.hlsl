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
    float4 gDemoSphereParams;
    float4 gWaterParams;
};

Texture2D gDisplacementMap : register(t2);
SamplerState gSampler : register(s0);

float SampleSmoothHeight(float2 uv)
{
    const float2 texel = float2(1.0f / 1024.0f, 1.0f / 1024.0f);
    float height = gDisplacementMap.SampleLevel(gSampler, uv, 0.0f).r * 0.5f;
    height += gDisplacementMap.SampleLevel(gSampler, uv + texel * float2(1.5f, 0.0f), 0.0f).r * 0.125f;
    height += gDisplacementMap.SampleLevel(gSampler, uv + texel * float2(-1.5f, 0.0f), 0.0f).r * 0.125f;
    height += gDisplacementMap.SampleLevel(gSampler, uv + texel * float2(0.0f, 1.5f), 0.0f).r * 0.125f;
    height += gDisplacementMap.SampleLevel(gSampler, uv + texel * float2(0.0f, -1.5f), 0.0f).r * 0.125f;
    return height;
}

struct DSInput
{
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
    float3 LocalPos : TEXCOORD3;
    float3 TangentW : TEXCOORD4;
};

struct PatchConstants
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

struct DSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
    float3 TangentW : TEXCOORD3;
    float2 WaterTex0 : TEXCOORD4;
    float2 WaterTex1 : TEXCOORD5;
};

[domain("tri")]
DSOutput DSMain(PatchConstants patchConstants,
    float3 bary : SV_DomainLocation,
    const OutputPatch<DSInput, 3> patch)
{
    DSOutput output;
    float3 localPos = patch[0].LocalPos * bary.x + patch[1].LocalPos * bary.y + patch[2].LocalPos * bary.z;
    float3 normalW = normalize(patch[0].NormalW * bary.x + patch[1].NormalW * bary.y + patch[2].NormalW * bary.z);
    float3 tangentW = normalize(patch[0].TangentW * bary.x + patch[1].TangentW * bary.y + patch[2].TangentW * bary.z);
    float2 tex = patch[0].Tex * bary.x + patch[1].Tex * bary.y + patch[2].Tex * bary.z;

    float time = gWaterParams.x;
    float waveSpeed = gWaterParams.y;
    float2 waterTex0 = tex + float2(time * waveSpeed, time * waveSpeed * 0.24f);
    float2 waterTex1 = tex * 1.37f + float2(-time * waveSpeed * 0.32f, time * waveSpeed * 0.41f);

    float h0 = SampleSmoothHeight(waterTex0);
    float h1 = SampleSmoothHeight(waterTex1);
    float smoothWave = sin(localPos.x * 0.025f + time * 0.75f) * 0.35f
        + sin(localPos.z * 0.032f - time * 0.58f) * 0.25f;
    float height = (((h0 + h1) * 0.5f - 0.5f) * 0.75f + smoothWave * 0.25f) * gMaterialParams.z;
    localPos += normalW * height;

    float4 posW = mul(float4(localPos, 1.0f), gWorld);
    output.PosW = posW.xyz;
    output.PosH = mul(float4(localPos, 1.0f), gWorldViewProj);
    output.NormalW = normalW;
    output.TangentW = tangentW;
    output.Tex = tex;
    output.WaterTex0 = waterTex0;
    output.WaterTex1 = waterTex1;
    return output;
}
