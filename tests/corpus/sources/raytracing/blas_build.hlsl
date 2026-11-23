// BLAS leaf pass: per-triangle bounds and Morton codes.

struct Triangle
{
    float3 v0;
    float3 v1;
    float3 v2;
};

struct AABB
{
    float3 minimum;
    float3 maximum;
};

struct Leaf
{
    AABB bounds;
    uint triangleIndex;
    uint morton;
    uint pad;
};

StructuredBuffer<Triangle> Triangles : register(t0);
RWStructuredBuffer<Leaf> Leaves : register(u0);
RWStructuredBuffer<uint> Counters : register(u1);

cbuffer Params : register(b0)
{
    uint triangleCount;
};

uint expandBits(uint value)
{
    value = (value * 0x00010001u) & 0xff0000ffu;
    value = (value * 0x00000101u) & 0x0f00f00fu;
    value = (value * 0x00000011u) & 0xc30c30c3u;
    value = (value * 0x00000005u) & 0x49249249u;
    return value;
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint index = tid.x;
    if (index >= triangleCount)
        return;

    Triangle tri = Triangles[index];

    Leaf leaf;
    leaf.bounds.minimum = min(tri.v0, min(tri.v1, tri.v2));
    leaf.bounds.maximum = max(tri.v0, max(tri.v1, tri.v2));
    leaf.triangleIndex = index;

    float3 center = (tri.v0 + tri.v1 + tri.v2) / 3.0;
    uint3 quantized = (uint3)clamp(center * 1023.0 + 0.5, 0.0, 1023.0);
    leaf.morton = expandBits(quantized.x) | (expandBits(quantized.y) << 1) | (expandBits(quantized.z) << 2);
    leaf.pad = 0;

    Leaves[index] = leaf;
    InterlockedAdd(Counters[0], 1u);
}
