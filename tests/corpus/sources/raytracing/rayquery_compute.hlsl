// Inline ray query against a TLAS.

RaytracingAccelerationStructure Scene : register(t0);
RWStructuredBuffer<uint> Hits : register(u0);
RWStructuredBuffer<float> Depths : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    RayDesc ray;
    ray.Origin = float3((float)tid.x * 0.01, 0.0, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 1e9;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(Scene, RAY_FLAG_NONE, 0xff, ray);

    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
            query.CommitNonOpaqueTriangleHit();
    }

    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        Hits[tid.x] = 1u;
        Depths[tid.x] = query.CommittedRayT();
    }
    else
    {
        Hits[tid.x] = 0u;
        Depths[tid.x] = 1e9;
    }
}
