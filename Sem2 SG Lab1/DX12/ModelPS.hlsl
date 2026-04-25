cbuffer ObjectCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;

    float3 gEyePosW;
    float pad0;

    float4 gLightDir;
    float4 gDiffuseColor;  // MTL Kd.
    float4 gSpecularColor; // MTL Ks.
    float gShininess;      // MTL Ns.
    float gTextureScaleX;
    float gTextureScaleY;
    float2 gTextureOffset;
};

Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

struct PSInput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    return (len2 < 1e-8f) ? float3(0.0f, 1.0f, 0.0f) : v * rsqrt(len2);
}

float4 PSMain(PSInput pin) : SV_Target
{
    float3 normal = SafeNormalize(pin.NormalW);
    float3 light = SafeNormalize(-gLightDir.xyz);
    float3 view = SafeNormalize(gEyePosW - pin.PosW);
    float3 halfVector = SafeNormalize(light + view);

    // Diffuse texture from map_Kd, sampled with WRAP mode for tiling.
    float4 texColor = gDiffuseMap.Sample(gSampler, pin.Tex);
    float3 baseColor = texColor.rgb * gDiffuseColor.rgb;
    float ndotl = saturate(dot(normal, light));

    float3 ambient = 0.18f * baseColor;
    float3 diffuse = baseColor * ndotl;
    float3 specular = 0.0f;

    if (ndotl > 0.0f)
    {
        float specPower = pow(saturate(dot(normal, halfVector)), gShininess);
        specular = gSpecularColor.rgb * specPower;
    }

    return float4(ambient + diffuse + specular, texColor.a * gDiffuseColor.a);
}
