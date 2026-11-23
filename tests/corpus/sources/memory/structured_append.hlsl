// Append/consume structured buffers.

struct Particle
{
    float3 position;
    float life;
};

AppendStructuredBuffer<Particle> Spawned : register(u0);
ConsumeStructuredBuffer<Particle> Dead : register(u1);
RWStructuredBuffer<float4> Output : register(u2);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x >= 16)
        return;

    Particle p = Dead.Consume();
    p.life -= 0.125;

    if (p.life > 0.0)
        Spawned.Append(p);

    Output[tid.x] = float4(p.position, p.life);
}
