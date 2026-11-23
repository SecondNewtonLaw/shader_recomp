// Integer, float, bit and conversion operations.
// Compiles to DXBC SM5 and DXIL SM6.

RWStructuredBuffer<float4> Output : register(u0);
StructuredBuffer<float4> Input : register(t0);

cbuffer Params : register(b0)
{
    float4 scale;
    uint mode;
};

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint i = tid.x;

    float4 a = Input[i];
    float4 b = Input[i + 1];

    float4 r = mad(a, b, scale);
    r = saturate(r);

    uint bits = asuint(r.x);
    bits = (bits << 5) | (bits >> 27);
    bits ^= mode;

    r.y = (float)(bits & 0xffffu);
    r.z = clamp(r.z, -2.0, 2.0);
    r.w = dot(a.xyz, b.xyz);

    Output[i] = r;
}
