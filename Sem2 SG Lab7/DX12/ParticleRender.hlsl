struct Particle
{
    float3 Position;
    float Age;
    float3 Velocity;
    float LifetimeScale;
    float4 Color;
    float Size;
    uint Seed;
    float2 Padding;
};

cbuffer ParticleCB : register(b0)
{
    float4x4 gViewProj;
    float4 gCameraRightSize;
    float4 gCameraUp;
    float4 gCameraForward;
    float4 gColor;
};

StructuredBuffer<Particle> gParticles : register(t0);

struct VSOutput
{
    float3 PosW : POSITION;
    float4 Color : COLOR0;
    float Size : SIZE0;
    float Alive : TEXCOORD0;
};

struct GSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 Tex : TEXCOORD2;
    float4 Color : COLOR0;
};

struct GBufferOutput
{
    float4 AlbedoSpec : SV_Target0;
    float4 NormalShininess : SV_Target1;
    float4 Position : SV_Target2;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    Particle p = gParticles[vertexId];

    VSOutput output;
    output.PosW = p.Position;
    output.Color = p.Color * gColor;
    output.Size = p.Size * gCameraRightSize.w;
    output.Alive = p.Age > 0.0f ? 1.0f : 0.0f;
    return output;
}

[maxvertexcount(4)]
void GSMain(point VSOutput input[1], inout TriangleStream<GSOutput> stream)
{
    if (input[0].Alive <= 0.0f)
        return;

    float3 right = normalize(gCameraRightSize.xyz) * input[0].Size;
    float3 up = normalize(gCameraUp.xyz) * input[0].Size;
    float3 center = input[0].PosW;
    float2 uv[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };
    float3 corners[4] =
    {
        center - right - up,
        center - right + up,
        center + right - up,
        center + right + up
    };

    [unroll]
    for (uint i = 0; i < 4; ++i)
    {
        GSOutput output;
        output.PosW = corners[i];
        output.PosH = mul(float4(corners[i], 1.0f), gViewProj);
        output.NormalW = -normalize(gCameraForward.xyz);
        output.Tex = uv[i];
        output.Color = input[0].Color;
        stream.Append(output);
    }
}

GBufferOutput PSMain(GSOutput input)
{
    float2 local = input.Tex * 2.0f - 1.0f;
    if (dot(local, local) > 1.0f)
        discard;

    GBufferOutput output;
    output.AlbedoSpec = float4(input.Color.rgb, 0.05f);
    output.NormalShininess = float4(normalize(input.NormalW) * 0.5f + 0.5f, 0.05f);
    output.Position = float4(input.PosW, 1.0f);
    return output;
}
