struct Light
{
    float4 PositionRange;
    float4 DirectionSpot;
    float4 ColorIntensity;
    float4 Params;
};

cbuffer LightingCB : register(b0)
{
    float3 gEyePosW;
    float gLightCount;
    float4 gAmbientColor;
    Light gLights[16];
    float4x4 gView;
    float4x4 gShadowViewProj[4];
    float4 gCascadeSplits;
    float4 gShadowTexelSizeBias; // x = texel size, y = base bias, z = slope bias.
};

Texture2D gAlbedoMetallic : register(t0);
Texture2D gNormalRoughness : register(t1);
Texture2D gPositionAO : register(t2);
Texture2DArray<float> gShadowMap : register(t3);
TextureCube gIrradianceMap : register(t4);
TextureCube gPrefilteredEnvMap : register(t5);
Texture2D gBrdfIntegrationMap : register(t6);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

static const float PI = 3.14159265359f;
static const float MAX_REFLECTION_LOD = 11.0f;

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    return (len2 < 1e-8f) ? float3(0.0f, 1.0f, 0.0f) : v * rsqrt(len2);
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float ndoth = saturate(dot(normal, halfVector));
    float ndoth2 = ndoth * ndoth;
    float denom = ndoth2 * (a2 - 1.0f) + 1.0f;
    return a2 / max(PI * denom * denom, 0.00001f);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return ndotv / max(ndotv * (1.0f - k) + k, 0.00001f);
}

float GeometrySmith(float3 normal, float3 viewDir, float3 lightDir, float roughness)
{
    float ndotv = saturate(dot(normal, viewDir));
    float ndotl = saturate(dot(normal, lightDir));
    return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    return f0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), f0) - f0)
        * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 EvaluateLight(Light light, float3 posW, float3 normal, float3 viewDir, float3 albedo, float roughness, float metallic)
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
    if (ndotl <= 0.0f)
        return 0.0f;

    float3 halfVector = SafeNormalize(lightDir + viewDir);
    float3 radiance = light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation;

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float ndf = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);
    float3 fresnel = FresnelSchlick(saturate(dot(halfVector, viewDir)), f0);

    float denominator = max(4.0f * saturate(dot(normal, viewDir)) * ndotl, 0.00001f);
    float3 specular = (ndf * geometry * fresnel) / denominator;

    float3 kS = fresnel;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * ndotl;
}

int SelectCascade(float viewDepth)
{
    if (viewDepth <= gCascadeSplits.x) return 0;
    if (viewDepth <= gCascadeSplits.y) return 1;
    if (viewDepth <= gCascadeSplits.z) return 2;
    return 3;
}

float DirectionalShadow(float3 posW, float3 normalW, float3 lightDir)
{
    float viewDepth = mul(float4(posW, 1.0f), gView).z;
    int cascade = SelectCascade(viewDepth);

    float4 shadowPos = mul(float4(posW, 1.0f), gShadowViewProj[cascade]);
    shadowPos.xyz /= shadowPos.w;

    float2 uv = float2(shadowPos.x * 0.5f + 0.5f, -shadowPos.y * 0.5f + 0.5f);
    if (any(uv < 0.0f) || any(uv > 1.0f) || shadowPos.z < 0.0f || shadowPos.z > 1.0f)
        return 1.0f;

    float ndotl = saturate(dot(normalW, lightDir));
    float compareDepth = shadowPos.z - max(gShadowTexelSizeBias.y, gShadowTexelSizeBias.z * (1.0f - ndotl));
    float texelSize = gShadowTexelSizeBias.x;
    float visibility = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2((float)x, (float)y) * texelSize;
            visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(uv + offset, (float)cascade), compareDepth);
        }
    }

    visibility /= 9.0f;
    visibility = visibility * visibility;
    return lerp(gShadowTexelSizeBias.w, 1.0f, visibility);
}

float4 PSMain(PSInput pin) : SV_Target
{
    float4 albedoMetallic = gAlbedoMetallic.Sample(gSampler, pin.Tex);
    float4 normalRoughness = gNormalRoughness.Sample(gSampler, pin.Tex);
    float4 positionAO = gPositionAO.Sample(gSampler, pin.Tex);

    if (dot(albedoMetallic.rgb, albedoMetallic.rgb) <= 0.0001f)
        return float4(0.05f, 0.06f, 0.075f, 1.0f);

    float3 albedo = albedoMetallic.rgb;
    float metallic = saturate(albedoMetallic.a);
    float3 normal = SafeNormalize(normalRoughness.xyz * 2.0f - 1.0f);
    float roughness = clamp(normalRoughness.a, 0.04f, 1.0f);
    float ao = saturate(positionAO.a);
    float3 viewDir = SafeNormalize(gEyePosW - positionAO.xyz);

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 ambientFresnel = FresnelSchlickRoughness(saturate(dot(normal, viewDir)), f0, roughness);
    float3 kD = (1.0f - ambientFresnel) * (1.0f - metallic);
    float3 irradiance = gIrradianceMap.Sample(gSampler, normal).rgb;
    float3 diffuse = irradiance * albedo;
    float3 reflection = reflect(-viewDir, normal);
    float3 prefilteredColor = gPrefilteredEnvMap.SampleLevel(gSampler, reflection, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = gBrdfIntegrationMap.Sample(gSampler, float2(saturate(dot(normal, viewDir)), roughness)).rg;
    float3 specular = prefilteredColor * (ambientFresnel * brdf.x + brdf.y);
    float3 lighting = (kD * diffuse + specular) * ao + gAmbientColor.rgb * albedo * 0.03f * ao;

    [loop]
    for (int i = 0; i < (int)gLightCount && i < 16; ++i)
    {
        float3 lightContribution = EvaluateLight(gLights[i], positionAO.xyz, normal, viewDir, albedo, roughness, metallic);
        if (i == 0 && gLights[i].Params.x < 0.5f)
        {
            float3 lightDir = SafeNormalize(-gLights[i].DirectionSpot.xyz);
            lightContribution *= DirectionalShadow(positionAO.xyz, normal, lightDir);
        }
        lighting += lightContribution;
    }

    return float4(lighting, 1.0f);
}
