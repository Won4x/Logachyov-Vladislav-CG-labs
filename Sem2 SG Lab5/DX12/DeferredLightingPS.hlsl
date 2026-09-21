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

Texture2D gAlbedoSpec : register(t0);
Texture2D gNormalShininess : register(t1);
Texture2D gPosition : register(t2);
Texture2DArray<float> gShadowMap : register(t3);
Texture2D gCascadeCat : register(t4);
Texture2D gCascadeDog : register(t5);
Texture2D gCascadeGuineaPig : register(t6);
Texture2D gCascadeDolphin : register(t7);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

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

float3 EvaluateLight(Light light, float3 posW, float3 normal, float3 viewDir, float specularStrength, float shininess)
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
    float specular = ndotl > 0.0f ? pow(saturate(dot(normal, halfVector)), shininess) * specularStrength : 0.0f;

    return light.ColorIntensity.rgb * light.ColorIntensity.w * attenuation * (ndotl + specular);
}

int SelectCascade(float viewDepth)
{
    if (viewDepth <= gCascadeSplits.x) return 0;
    if (viewDepth <= gCascadeSplits.y) return 1;
    if (viewDepth <= gCascadeSplits.z) return 2;
    return 3;
}

float4 SampleCascadeOverlay(int cascade, float2 uv)
{
    float2 tiledUv = frac(uv * 50.0f);
    float4 result = gCascadeDolphin.SampleLevel(gSampler, tiledUv, 0.0f);

    if (cascade == 0)
        result = gCascadeCat.SampleLevel(gSampler, tiledUv, 0.0f);
    else if (cascade == 1)
        result = gCascadeDog.SampleLevel(gSampler, tiledUv, 0.0f);
    else if (cascade == 2)
        result = gCascadeGuineaPig.SampleLevel(gSampler, tiledUv, 0.0f);

    return result;
}

float DirectionalShadow(float3 posW, float3 normalW, float3 lightDir, out int cascade, out float2 shadowUv)
{
    float viewDepth = mul(float4(posW, 1.0f), gView).z;
    cascade = SelectCascade(viewDepth);

    float4 shadowPos = mul(float4(posW, 1.0f), gShadowViewProj[cascade]);
    shadowPos.xyz /= shadowPos.w;

    float2 uv = float2(shadowPos.x * 0.5f + 0.5f, -shadowPos.y * 0.5f + 0.5f);
    shadowUv = uv;
    if (any(uv < 0.0f) || any(uv > 1.0f) || shadowPos.z < 0.0f || shadowPos.z > 1.0f)
        return 1.0f;

    float ndotl = saturate(dot(normalW, lightDir));
    float compareDepth = shadowPos.z - max(gShadowTexelSizeBias.y, gShadowTexelSizeBias.z * (1.0f - ndotl));
    float texelSize = gShadowTexelSizeBias.x;
    float pcfRadius = texelSize * 0.75f;
    float visibility = 0.0f;
    visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(uv + float2(-pcfRadius, -pcfRadius), (float)cascade), compareDepth);
    visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(uv + float2( pcfRadius, -pcfRadius), (float)cascade), compareDepth);
    visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(uv + float2(-pcfRadius,  pcfRadius), (float)cascade), compareDepth);
    visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(uv + float2( pcfRadius,  pcfRadius), (float)cascade), compareDepth);

    visibility *= 0.25f;
    visibility = visibility * visibility * visibility;
    return lerp(gShadowTexelSizeBias.w, 1.0f, visibility);
}

float4 PSMain(PSInput pin) : SV_Target
{
    float4 albedoSpec = gAlbedoSpec.Sample(gSampler, pin.Tex);
    float4 normalShininess = gNormalShininess.Sample(gSampler, pin.Tex);
    float4 position = gPosition.Sample(gSampler, pin.Tex);

    if (dot(albedoSpec.rgb, albedoSpec.rgb) <= 0.0001f)
        return float4(0.05f, 0.06f, 0.075f, 1.0f);

    float3 albedo = albedoSpec.rgb;
    float specularStrength = albedoSpec.a;
    float3 normal = SafeNormalize(normalShininess.xyz * 2.0f - 1.0f);
    float shininess = lerp(8.0f, 256.0f, normalShininess.a);
    float3 viewDir = SafeNormalize(gEyePosW - position.xyz);

    float3 lighting = gAmbientColor.rgb;
    float3 cascadeOverlayColor = 0.0f;
    float cascadeOverlayAlpha = 0.0f;
    [loop]
    for (int i = 0; i < (int)gLightCount && i < 16; ++i)
    {
        float3 lightContribution = EvaluateLight(gLights[i], position.xyz, normal, viewDir, specularStrength, shininess);
        if (i == 0 && gLights[i].Params.x < 0.5f)
        {
            float3 lightDir = SafeNormalize(-gLights[i].DirectionSpot.xyz);
            int shadowCascade = 0;
            float2 shadowUv = 0.0f;
            float shadowVisibility = DirectionalShadow(position.xyz, normal, lightDir, shadowCascade, shadowUv);

            float shadowAmount = saturate(1.0f - shadowVisibility);
            float4 overlay = SampleCascadeOverlay(shadowCascade, shadowUv);
            cascadeOverlayColor = overlay.rgb;
            cascadeOverlayAlpha = shadowAmount * lerp(0.75f, 1.0f, saturate(overlay.a));
        }
        lighting += lightContribution;
    }

    float3 color = albedo * lighting;
    color = color / (color + 1.0f);
    color = pow(saturate(color), 1.0f / 2.2f);
    color = lerp(color, cascadeOverlayColor, saturate(cascadeOverlayAlpha));
    return float4(color, 1.0f);
}
