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

struct Light
{
    float4 PositionRange;
    float4 DirectionSpot;
    float4 ColorIntensity;
    float4 Params;
};

cbuffer LightingCB : register(b1)
{
    float3 gEyePosW;
    float gLightCount;
    float4 gAmbientColor;
    Light gLights[16];
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
    float2 WaterTex0 : TEXCOORD4;
    float2 WaterTex1 : TEXCOORD5;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    return (len2 < 1e-8f) ? float3(0.0f, 1.0f, 0.0f) : v * rsqrt(len2);
}

float3 EvaluateLight(Light light, float3 posW, float3 normal, float3 viewDir)
{
    float type = light.Params.x;
    float3 lightDir = 0.0f;
    float attenuation = 1.0f;

    if (type < 0.5f)
    {
        lightDir = SafeNormalize(-light.DirectionSpot.xyz);
    }
    else
    {
        float3 toLight = light.PositionRange.xyz - posW;
        float distanceToLight = length(toLight);
        lightDir = distanceToLight > 1e-4f ? toLight / distanceToLight : float3(0.0f, 1.0f, 0.0f);
        attenuation = saturate(1.0f - distanceToLight / max(light.PositionRange.w, 0.001f));
        attenuation *= attenuation;

        if (type > 1.5f)
        {
            float3 spotDirection = SafeNormalize(light.DirectionSpot.xyz);
            float spot = saturate(dot(-lightDir, spotDirection));
            attenuation *= pow(spot, max(light.DirectionSpot.w, 1.0f));
        }
    }

    float ndotl = saturate(dot(normal, lightDir));
    float3 halfVector = SafeNormalize(lightDir + viewDir);
    float specular = ndotl > 0.0f ? pow(saturate(dot(normal, halfVector)), gMaterialParams.x) * 1.35f : 0.0f;
    return light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation * (ndotl * 0.65f + specular);
}

float4 PSMain(PSInput pin) : SV_Target
{
    float4 texColor = gDiffuseMap.Sample(gSampler, pin.Tex);
    float3 normalW = SafeNormalize(pin.NormalW);
    float3 tangentW = SafeNormalize(pin.TangentW - normalW * dot(pin.TangentW, normalW));
    float3 bitangentW = SafeNormalize(cross(normalW, tangentW));

    float3 n0 = gNormalMap.Sample(gSampler, pin.WaterTex0).xyz * 2.0f - 1.0f;
    float3 n1 = gNormalMap.Sample(gSampler, pin.WaterTex1).xyz * 2.0f - 1.0f;
    float3 normalSample = SafeNormalize(float3(n0.xy + n1.xy * 0.65f, n0.z + n1.z));
    normalSample.xy *= max(gMaterialParams.y, 1.0f);
    normalSample = SafeNormalize(normalSample);
    normalW = SafeNormalize(normalSample.x * tangentW + normalSample.y * bitangentW + normalSample.z * normalW);

    float3 viewDir = SafeNormalize(gEyePosW - pin.PosW);
    float fresnel = pow(1.0f - saturate(dot(viewDir, normalW)), 4.0f);

    float3 deepWater = float3(0.02f, 0.23f, 0.32f);
    float3 shallowWater = gDiffuseColor.rgb * texColor.rgb;
    float3 color = lerp(deepWater, shallowWater, 0.72f);

    float3 lighting = gAmbientColor.rgb * 1.6f;
    [loop]
    for (int i = 0; i < (int)gLightCount && i < 16; ++i)
        lighting += EvaluateLight(gLights[i], pin.PosW, normalW, viewDir);

    color *= lighting;
    color += fresnel * float3(0.28f, 0.55f, 0.72f);

    return float4(saturate(color), gWaterParams.z);
}
