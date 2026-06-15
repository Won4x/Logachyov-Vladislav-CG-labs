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
};

Texture2D gAlbedoSpec : register(t0);
Texture2D gNormalShininess : register(t1);
Texture2D gPosition : register(t2);
SamplerState gSampler : register(s0);

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
    [loop]
    for (int i = 0; i < (int)gLightCount && i < 16; ++i)
        lighting += EvaluateLight(gLights[i], position.xyz, normal, viewDir, specularStrength, shininess);

    float3 color = albedo * lighting;
    color = color / (color + 1.0f);
    color = pow(saturate(color), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
