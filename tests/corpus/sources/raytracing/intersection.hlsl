// Procedural primitive intersection shader (unit sphere).

struct IntersectionAttributes
{
    float2 barycentrics;
};

[shader("intersection")]
void Intersection()
{
    float3 origin = ObjectRayOrigin();
    float3 direction = ObjectRayDirection();

    float b = dot(origin, direction);
    float c = dot(origin, origin) - 1.0;
    float discriminant = b * b - c;

    if (discriminant < 0.0)
        return;

    float t = -b - sqrt(discriminant);
    if (t < RayTMin() || t > RayTCurrent())
        return;

    IntersectionAttributes attributes;
    attributes.barycentrics = float2(t, 0.0);

    ReportHit(t, 0, attributes);
}
