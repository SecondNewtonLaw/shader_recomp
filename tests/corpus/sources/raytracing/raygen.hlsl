// Ray generation shader tracing into a TLAS.

RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> Output : register(u0);

cbuffer Camera : register(b0)
{
    float4x4 inverseViewProjection;
    float3 cameraPosition;
};

struct [raypayload] Payload
{
    float3 color : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
    float depth : write(caller, closesthit, miss, anyhit) : read(caller, closesthit, miss, anyhit);
};

[shader("raygeneration")]
void RayGen()
{
    uint2 pixel = DispatchRaysIndex().xy;
    uint2 size = DispatchRaysDimensions().xy;

    float2 ndc = (pixel + 0.5) / (float2)size * 2.0 - 1.0;
    float4 world = mul(float4(ndc, 0.0, 1.0), inverseViewProjection);
    world.xyz /= world.w;

    RayDesc ray;
    ray.Origin = cameraPosition;
    ray.Direction = normalize(world.xyz - cameraPosition);
    ray.TMin = 0.001;
    ray.TMax = 10000.0;

    Payload payload;
    payload.color = float3(0.0, 0.0, 0.0);
    payload.depth = 1e9;

    TraceRay(Scene, RAY_FLAG_NONE, 0xff, 0, 1, 0, ray, payload);

    Output[pixel] = float4(payload.color, saturate(payload.depth * 0.01));
}
