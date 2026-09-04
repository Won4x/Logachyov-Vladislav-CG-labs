cbuffer ObjectCB : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;

    float3 gEyePosW;
    float pad0;

    float4 gLightDir;
    float4 gDiffuseColor;
    float4 gSpecularColor;
    float gShininess;
    float3 pad1;
};

struct PSInput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float3 PosL : TEXCOORD2;
};

float3 SafeNormalize(float3 v)
{
    float len2 = dot(v, v);
    if (len2 < 1e-8f)
        return float3(0.0f, 0.0f, 1.0f);

    return v * rsqrt(len2);
}

float4 PSMain(PSInput pin) : SV_Target
{
    float3 normal = SafeNormalize(pin.NormalW);
    float3 light = SafeNormalize(-gLightDir.xyz);
    float3 view = SafeNormalize(gEyePosW - pin.PosW);
    float3 halfVector = SafeNormalize(light + view);

    float3 baseColor = saturate(0.5f * (pin.PosL + 1.0f));
    float ndotl = saturate(dot(normal, light));

    float3 ambient = 0.2f * baseColor;
    float3 diffuse = baseColor * ndotl;

    float3 specular = 0.0f;
    if (ndotl > 0.0f)
    {
        float specPower = pow(saturate(dot(normal, halfVector)), gShininess);
        specular = gSpecularColor.rgb * specPower;
    }

    return float4(ambient + diffuse + specular, 1.0f);
}
