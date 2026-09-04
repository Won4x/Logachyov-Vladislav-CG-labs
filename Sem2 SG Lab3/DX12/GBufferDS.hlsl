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

Texture2D gDisplacementMap : register(t2);
SamplerState gSampler : register(s0);

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

    float height = dot(gDisplacementMap.SampleLevel(gSampler, tex, 0.0f).rgb, float3(0.299f, 0.587f, 0.114f));
    float centeredHeight = height - 0.5f;
    float3 displacedLocalPos = localPos + normalW * (centeredHeight * gEyeDisplacement.w);
    float4 posW = mul(float4(displacedLocalPos, 1.0f), gWorld);

    output.PosW = posW.xyz;
    output.PosH = mul(float4(displacedLocalPos, 1.0f), gWorldViewProj);
    output.NormalW = normalW;
    output.TangentW = tangentW;
    output.Tex = tex;
    return output;
}
