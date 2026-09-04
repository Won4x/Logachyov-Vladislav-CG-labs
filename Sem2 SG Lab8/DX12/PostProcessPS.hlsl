cbuffer PostProcessCB : register(b0)
{
    float4 gScreenSizeTime; // xy = pixel size, z = time.
    float4 gEffectFlags;    // x = gamma, y = dithering, z = vignette, w = chromatic aberration.
    float4 gEffectParams;   // x = post enabled, y = grain, z = exposure, w = dither strength.
};

Texture2D gSceneColor : register(t0);
SamplerState gLinearClamp : register(s0);

struct PSInput
{
    float4 PosH : SV_POSITION;
    float2 Tex : TEXCOORD0;
};

static const float4x4 Bayer4x4 =
{
    1.0f / 17.0f, 9.0f / 17.0f, 3.0f / 17.0f, 11.0f / 17.0f,
    13.0f / 17.0f, 5.0f / 17.0f, 15.0f / 17.0f, 7.0f / 17.0f,
    4.0f / 17.0f, 12.0f / 17.0f, 2.0f / 17.0f, 10.0f / 17.0f,
    16.0f / 17.0f, 8.0f / 17.0f, 14.0f / 17.0f, 6.0f / 17.0f
};

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float3 SampleScene(float2 uv)
{
    return gSceneColor.SampleLevel(gLinearClamp, saturate(uv), 0.0f).rgb;
}

float3 ApplyToneMap(float3 hdrColor)
{
    hdrColor *= max(gEffectParams.z, 0.001f);
    return hdrColor / (hdrColor + 1.0f);
}

float3 ApplyDithering(float3 color, float2 pixelPos)
{
    float threshold = Bayer4x4[(uint)pixelPos.y & 3][(uint)pixelPos.x & 3];
    float ditherStrength = max(gEffectParams.w, 1.0f);
    float3 quantized = color * 255.0f;
    float3 lower = floor(quantized);
    float3 delta = quantized - lower;
    float3 upper = min(lower + ditherStrength, 255.0f);

    color.r = (delta.r > threshold ? upper.r : lower.r) / 255.0f;
    color.g = (delta.g > threshold ? upper.g : lower.g) / 255.0f;
    color.b = (delta.b > threshold ? upper.b : lower.b) / 255.0f;
    return color;
}

float4 PSMain(PSInput pin) : SV_Target
{
    float2 uv = pin.Tex;
    float2 centered = uv - 0.5f;
    float3 hdrColor = SampleScene(uv);

    if (gEffectParams.x > 0.5f && gEffectFlags.w > 0.5f)
    {
        float2 aberrationOffset = centered * 0.006f;
        hdrColor.r = SampleScene(uv + aberrationOffset).r;
        hdrColor.b = SampleScene(uv - aberrationOffset).b;
    }

    float3 color = gEffectParams.x > 0.5f ? ApplyToneMap(hdrColor) : saturate(hdrColor);

    if (gEffectParams.x > 0.5f && gEffectFlags.z > 0.5f)
    {
        float vignette = smoothstep(0.58f, 0.08f, dot(centered, centered));
        color *= lerp(0.42f, 1.0f, vignette);
    }

    if (gEffectParams.x > 0.5f && gEffectParams.y > 0.5f)
    {
        float grain = Hash12(pin.PosH.xy + gScreenSizeTime.z * 97.0f) - 0.5f;
        color += grain * 0.018f;
    }

    color = saturate(color);

    if (gEffectParams.x > 0.5f && gEffectFlags.x > 0.5f)
        color = pow(color, 1.0f / 2.2f);

    if (gEffectParams.x > 0.5f && gEffectFlags.y > 0.5f)
        color = ApplyDithering(saturate(color), pin.PosH.xy);

    return float4(saturate(color), 1.0f);
}
