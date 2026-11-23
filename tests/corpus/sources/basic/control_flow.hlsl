// Branches, loops, switch and early return.

RWStructuredBuffer<uint> Output : register(u0);

cbuffer Params : register(b0)
{
    uint count;
    uint limit;
};

uint mix(uint x, uint y)
{
    return x * 1664525u + y * 1013904223u;
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint x = tid.x;
    uint acc = 0;

    for (uint i = 0; i < count; ++i)
    {
        if ((i & 3u) == 0u)
            acc += mix(x, i);
        else if ((i & 3u) == 1u)
            acc ^= mix(x, i + 1u);
        else
            acc -= i;
    }

    switch (x & 3u)
    {
    case 0u:
        acc += limit;
        break;
    case 1u:
        acc *= 3u;
        break;
    case 2u:
        acc = ~acc;
        break;
    default:
        acc ^= 0xa5a5a5a5u;
        break;
    }

    if (acc > limit)
        return;

    Output[x] = acc;
}
