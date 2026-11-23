// Typed buffers, bit reinterpretation and scalar indexing.

Buffer<float4> FloatInput : register(t0);
Buffer<uint> BitInput : register(t1);
RWBuffer<float4> FloatOutput : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float4 v = FloatInput[tid.x];
    uint bits = BitInput[tid.x];

    v.x = asfloat(bits);
    v.y = (float)(bits & 0xffffu);
    v.z = (float)(bits >> 16);
    v.w = asfloat((bits << 7) | (bits >> 25));

    FloatOutput[tid.x] = v;
}
