// Software TLAS traversal over instance bounds and triangles.

#pragma warning(disable : 4008) // intentional division by zero in the slab test

struct AABB
{
    float3 minimum;
    float3 maximum;
};

struct InstanceBounds
{
    AABB bounds;
    uint instance;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct Triangle
{
    float3 v0;
    float3 v1;
    float3 v2;
};

struct HitResult
{
    uint instance;
    uint triangleIndex;
    float t;
    uint pad;
};

StructuredBuffer<InstanceBounds> Instances : register(t0);
StructuredBuffer<Triangle> Triangles : register(t1);
RWStructuredBuffer<HitResult> Output : register(u0);

cbuffer Params : register(b0)
{
    uint instanceCount;
    uint triangleCount;
};

bool rayAABB(float3 origin, float3 direction, AABB bounds, out float tMin, out float tMax)
{
    float3 inverse = 1.0 / direction;
    float3 t0 = (bounds.minimum - origin) * inverse;
    float3 t1 = (bounds.maximum - origin) * inverse;
    float3 smaller = min(t0, t1);
    float3 larger = max(t0, t1);

    tMin = max(max(smaller.x, smaller.y), max(smaller.z, 0.0));
    tMax = min(min(larger.x, larger.y), min(larger.z, 1e30));
    return tMax >= tMin;
}

bool rayTriangle(float3 origin, float3 direction, Triangle tri, out float t)
{
    float3 edge1 = tri.v1 - tri.v0;
    float3 edge2 = tri.v2 - tri.v0;
    float3 p = cross(direction, edge2);
    float determinant = dot(edge1, p);

    if (abs(determinant) < 1e-8)
    {
        t = 0.0;
        return false;
    }

    float inverse = 1.0 / determinant;
    float3 s = origin - tri.v0;
    float u = dot(s, p) * inverse;
    if (u < 0.0 || u > 1.0)
    {
        t = 0.0;
        return false;
    }

    float3 q = cross(s, edge1);
    float v = dot(direction, q) * inverse;
    if (v < 0.0 || u + v > 1.0)
    {
        t = 0.0;
        return false;
    }

    t = dot(edge2, q) * inverse;
    return t > 0.0;
}

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float3 origin = float3((float)tid.x * 0.01, 0.0, 0.0);
    float3 direction = float3(0.0, 0.0, 1.0);

    HitResult best;
    best.instance = 0xffffffffu;
    best.triangleIndex = 0xffffffffu;
    best.t = 1e30;
    best.pad = 0;

    for (uint i = 0; i < instanceCount; ++i)
    {
        float tMin;
        float tMax;
        if (!rayAABB(origin, direction, Instances[i].bounds, tMin, tMax))
            continue;

        for (uint j = 0; j < triangleCount; ++j)
        {
            float t;
            if (rayTriangle(origin, direction, Triangles[j], t) && t < best.t)
            {
                best.instance = Instances[i].instance;
                best.triangleIndex = j;
                best.t = t;
            }
        }
    }

    Output[tid.x] = best;
}
