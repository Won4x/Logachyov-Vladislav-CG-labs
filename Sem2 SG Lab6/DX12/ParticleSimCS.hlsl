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

cbuffer ParticleSimCB : register(b0)
{
    float4 gEmitterPositionTime; // xyz = emitter, w = total time.
    float4 gGravityDeltaTime;    // xyz = gravity, w = delta time.
    float4 gEmitterParams;       // x = pool size, y = spawn chance, z = base lifetime, w = velocity spread.
};

ConsumeStructuredBuffer<Particle> gInputParticles : register(u0);
AppendStructuredBuffer<Particle> gOutputParticles : register(u1);

uint Hash(uint state)
{
    state ^= state >> 16;
    state *= 2246822519u;
    state ^= state >> 13;
    state *= 3266489917u;
    state ^= state >> 16;
    return state;
}

float Random01(inout uint state)
{
    state = Hash(state);
    return (float)(state & 0x00ffffffu) / 16777216.0f;
}

float3 RandomDirectionInCone(inout uint state)
{
    float angle = Random01(state) * 6.2831853f;
    float radius = sqrt(Random01(state));
    float y = 0.35f + Random01(state) * 0.65f;
    float horizontal = sqrt(saturate(1.0f - y * y)) * radius;
    return normalize(float3(cos(angle) * horizontal, y, sin(angle) * horizontal));
}

Particle SpawnParticle(uint seed)
{
    uint state = seed;
    float spawnAngle = Random01(state) * 6.2831853f;
    float spawnRadius = sqrt(Random01(state)) * 5.0f;
    float3 direction = RandomDirectionInCone(state);
    float speed = 22.0f + Random01(state) * 20.0f;

    Particle p;
    p.Position = gEmitterPositionTime.xyz + float3(cos(spawnAngle) * spawnRadius, 0.0f, sin(spawnAngle) * spawnRadius);
    p.Age = 0.0001f;
    p.Velocity = direction * speed + float3(
        (Random01(state) - 0.5f) * gEmitterParams.w,
        0.0f,
        (Random01(state) - 0.5f) * gEmitterParams.w);
    p.LifetimeScale = 0.65f + Random01(state) * 0.7f;
    p.Color = float4(1.0f, 0.38f + Random01(state) * 0.28f, 0.05f, 1.0f);
    p.Size = 0.9f + Random01(state) * 1.4f;
    p.Seed = Hash(state + 1013904223u);
    p.Padding = 0.0f;
    return p;
}

[numthreads(256, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint id = dispatchThreadId.x;
    if (id >= (uint)gEmitterParams.x)
        return;

    Particle p = gInputParticles.Consume();
    float dt = gGravityDeltaTime.w;

    float currentLifetime = max(0.05f, gEmitterParams.z * max(p.LifetimeScale, 0.1f));

    if (p.Age > 0.0f && p.Age < currentLifetime)
    {
        uint forceSeed = Hash(p.Seed + (uint)(p.Age * 120.0f) * 747796405u);
        float3 turbulence = float3(
            Random01(forceSeed) - 0.5f,
            (Random01(forceSeed) - 0.5f) * 0.35f,
            Random01(forceSeed) - 0.5f) * 9.0f;

        p.Velocity += (gGravityDeltaTime.xyz + turbulence) * dt;
        p.Position += p.Velocity * dt;
        p.Age += dt;
        p.Size += dt * 0.18f;

        float life = saturate(p.Age / currentLifetime);
        p.Color.rgb = lerp(float3(1.0f, 0.52f, 0.08f), float3(0.85f, 0.05f, 0.02f), life);
        p.Color.a = 1.0f;

        if (p.Position.y < 1.0f || p.Age >= currentLifetime)
            p.Age = 0.0f;
    }
    else
    {
        p.Seed = Hash(p.Seed + id * 1664525u + (uint)(gEmitterPositionTime.w * 1000.0f));
        uint spawnSeed = p.Seed;
        float spawn = Random01(spawnSeed);
        if (spawn < gEmitterParams.y)
            p = SpawnParticle(spawnSeed);
        else
        {
            p.Age = 0.0f;
            p.Seed = spawnSeed;
        }
    }

    gOutputParticles.Append(p);
}
