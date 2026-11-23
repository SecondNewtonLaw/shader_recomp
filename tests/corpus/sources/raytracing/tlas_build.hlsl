// TLAS instance pass: transform BLAS bounds into world space.

struct AABB
{
    float3 minimum;
    float3 maximum;
};

struct Instance
{
    float4x4 transform;
    uint blasOffset;
    uint blasCount;
    uint instanceId;
    uint mask;
};

struct InstanceBounds
{
    AABB bounds;
    uint instance;
    uint pad0;
    uint pad1;
    uint pad2;
};

StructuredBuffer<AABB> BlasBounds : register(t0);
StructuredBuffer<Instance> Instances : register(t1);
RWStructuredBuffer<InstanceBounds> Output : register(u0);
RWStructuredBuffer<uint> Counters : register(u1);

cbuffer Params : register(b0)
{
    uint instanceCount;
};

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint index = tid.x;
    if (index >= instanceCount)
        return;

    Instance instance = Instances[index];

    AABB bounds;
    bounds.minimum = float3(1e30, 1e30, 1e30);
    bounds.maximum = float3(-1e30, -1e30, -1e30);

    for (uint i = 0; i < instance.blasCount; ++i)
    {
        AABB local = BlasBounds[instance.blasOffset + i];

        for (uint cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
        {
            float3 corner = float3(
                (cornerIndex & 1) ? local.maximum.x : local.minimum.x,
                (cornerIndex & 2) ? local.maximum.y : local.minimum.y,
                (cornerIndex & 4) ? local.maximum.z : local.minimum.z);

            float3 world = mul(float4(corner, 1.0), instance.transform).xyz;
            bounds.minimum = min(bounds.minimum, world);
            bounds.maximum = max(bounds.maximum, world);
        }
    }

    InstanceBounds result;
    result.bounds = bounds;
    result.instance = instance.instanceId;
    result.pad0 = 0;
    result.pad1 = 0;
    result.pad2 = 0;

    Output[index] = result;
    InterlockedAdd(Counters[0], 1u);
}
