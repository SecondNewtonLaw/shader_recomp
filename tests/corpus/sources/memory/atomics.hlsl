// Atomics on global and groupshared memory, with returned values.

RWStructuredBuffer<uint> Counter : register(u0);
RWStructuredBuffer<uint> Data : register(u1);

groupshared uint localCount;

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x == 0)
        localCount = 0;

    GroupMemoryBarrierWithGroupSync();

    uint index;
    InterlockedAdd(localCount, 1u, index);

    uint previous;
    InterlockedCompareExchange(Data[0], tid.x, 0xffffffffu, previous);
    InterlockedMax(Data[1], tid.x);
    InterlockedAdd(Counter[0], 1u);

    Data[2 + index] = previous;
}
