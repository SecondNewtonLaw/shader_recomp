// Closest-hit shader for triangle geometry.

struct [raypayload] Payload
{
    float3 color : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
    float depth : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
};

[shader("closesthit")]
void ClosestHit(inout Payload payload, in BuiltInTriangleIntersectionAttributes attributes)
{
    float3 barycentrics = float3(
        1.0 - attributes.barycentrics.x - attributes.barycentrics.y,
        attributes.barycentrics.x,
        attributes.barycentrics.y);

    payload.color = barycentrics;
    payload.depth = RayTCurrent();
}
