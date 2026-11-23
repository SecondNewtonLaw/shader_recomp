// Miss shader.

struct [raypayload] Payload
{
    float3 color : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
    float depth : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
};

[shader("miss")]
void Miss(inout Payload payload)
{
    payload.color = float3(0.05, 0.10, 0.20) + WorldRayDirection();
    payload.depth = 1e9;
}
