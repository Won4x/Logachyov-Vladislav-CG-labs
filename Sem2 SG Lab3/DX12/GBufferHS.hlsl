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

struct HSInput
{
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
    float3 LocalPos : TEXCOORD3;
    float3 TangentW : TEXCOORD4;
};

struct HSOutput
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

float ComputeDistanceTessellation(float3 positionW)
{
    float distanceToCamera = distance(positionW, gEyeDisplacement.xyz);
    float maxTess = max(gTessellationParams.x, gTessellationParams.y);
    float minTess = min(gTessellationParams.x, gTessellationParams.y);
    float nearDistance = gTessellationParams.z;
    float farDistance = max(gTessellationParams.w, nearDistance + 1.0f);
    float lod = saturate((distanceToCamera - nearDistance) / (farDistance - nearDistance));
    return lerp(maxTess, minTess, lod);
}

float ComputeEdgeTessellation(float3 aW, float3 bW)
{
    return ComputeDistanceTessellation((aW + bW) * 0.5f);
}

PatchConstants PatchConstantMain(InputPatch<HSInput, 3> patch, uint patchId : SV_PrimitiveID)
{
    PatchConstants output;
    output.EdgeTess[0] = ComputeEdgeTessellation(patch[1].PosW, patch[2].PosW);
    output.EdgeTess[1] = ComputeEdgeTessellation(patch[2].PosW, patch[0].PosW);
    output.EdgeTess[2] = ComputeEdgeTessellation(patch[0].PosW, patch[1].PosW);
    output.InsideTess = (output.EdgeTess[0] + output.EdgeTess[1] + output.EdgeTess[2]) / 3.0f;
    return output;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstantMain")]
HSOutput HSMain(InputPatch<HSInput, 3> patch, uint controlPointId : SV_OutputControlPointID)
{
    HSOutput output;
    output.PosW = patch[controlPointId].PosW;
    output.NormalW = patch[controlPointId].NormalW;
    output.Tex = patch[controlPointId].Tex;
    output.LocalPos = patch[controlPointId].LocalPos;
    output.TangentW = patch[controlPointId].TangentW;
    return output;
}
