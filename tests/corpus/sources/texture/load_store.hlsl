// Texture load/store, multisample load and format conversion.

Texture2D<float4> Input : register(t0);
Texture2DMS<float4> Multisampled : register(t1);
RWTexture2D<float4> Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint2 p = tid.xy;

    float4 a = Input.Load(int3(p, 0));
    float4 b = Multisampled.Load(p, 0);
    float4 c = Input.Load(int3(p, 0), int2(1, -1));

    Output[p] = a + b + c;
}
