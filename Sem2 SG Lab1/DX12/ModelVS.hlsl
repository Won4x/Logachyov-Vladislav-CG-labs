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
    float gTextureScaleX;  // Texture tiling on U.
    float gTextureScaleY;  // Texture tiling on V.
    float gTextureOffsetX; // Animated UV scroll.
    float gTextureOffsetY;
    float3 gWindPadding;
    float4 gWindParams;    // x = time, y = curtain enable, z = strength, w = speed.
};

struct VSInput
{
    float3 Pos : POSITION;
    float3 Normal : NORMAL;
    float2 Tex : TEXCOORD;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
};

VSOutput VSMain(VSInput vin)
{
    VSOutput vout;

    float3 localPos = vin.Pos;
    if (gWindParams.y > 0.5f)
    {
        float time = gWindParams.x;
        float strength = gWindParams.z;
        float speed = gWindParams.w;

        float phase = localPos.y * 0.065f + localPos.z * 0.045f + vin.Tex.x * 6.28318f;
        float mainWave = sin(time * speed + phase);
        float smallWave = sin(time * (speed * 1.73f) + localPos.x * 0.08f + vin.Tex.y * 9.0f);
        float freeEdge = 0.25f + 0.75f * saturate(vin.Tex.y);
        float displacement = (mainWave + 0.35f * smallWave) * strength * freeEdge;

        localPos.x += displacement;
        localPos.z += displacement * 0.35f;
    }

    float4 posW = mul(float4(localPos, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.PosH = mul(posW, gWorldViewProj);
    vout.NormalW = normalize(mul(float4(vin.Normal, 0.0f), gWorld).xyz);
    // Tiling comes from scale, animation comes from offset.
    vout.Tex = vin.Tex * float2(gTextureScaleX, gTextureScaleY) + float2(gTextureOffsetX, gTextureOffsetY);

    return vout;
}
