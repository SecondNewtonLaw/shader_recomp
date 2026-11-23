// Function calls, structs and inlining.

struct Ray
{
    float3 origin;
    float3 direction;
};

struct Hit
{
    float t;
    float3 normal;
};

float3 reflect(float3 i, float3 n)
{
    return i - 2.0 * dot(i, n) * n;
}

float schlick(float cosine, float ri)
{
    float r0 = (1.0 - ri) / (1.0 + ri);
    r0 *= r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

Hit intersect(Ray ray, float3 center, float radius)
{
    Hit h;
    float3 oc = ray.origin - center;
    float b = dot(oc, ray.direction);
    float c = dot(oc, oc) - radius * radius;
    float disc = b * b - c;
    h.t = disc > 0.0 ? -b - sqrt(disc) : -1.0;
    h.normal = normalize(ray.origin + ray.direction * h.t - center);
    return h;
}

RWStructuredBuffer<float4> Output : register(u0);
StructuredBuffer<float4> Input : register(t0);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    Ray ray;
    ray.origin = Input[tid.x].xyz;
    ray.direction = normalize(Input[tid.x + 1].xyz - ray.origin);

    Hit h = intersect(ray, float3(0.0, 0.0, 0.0), 1.0);
    float3 n = h.t > 0.0 ? h.normal : float3(0.0, 1.0, 0.0);
    float3 r = reflect(ray.direction, n);
    float f = schlick(saturate(dot(-ray.direction, n)), 1.5);

    Output[tid.x] = float4(r, f + h.t);
}
