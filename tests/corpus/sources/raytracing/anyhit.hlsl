// Any-hit shader with ignore and accept-and-end-search paths.

struct [raypayload] Payload
{
    float3 color : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
    float depth : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
};

[shader("anyhit")]
void AnyHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    payload.depth = RayTCurrent();

    if (attributes.barycentrics.x < 0.01)
    {
        IgnoreHit();
    }
    else if (attributes.barycentrics.y > 0.99)
    {
        AcceptHitAndEndSearch();
    }
}
