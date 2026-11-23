// ByteAddressBuffer loads/stores and manual 64-bit composition.

ByteAddressBuffer Input : register(t0);
RWByteAddressBuffer Output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint offset = tid.x * 32;

    uint4 a = Input.Load4(offset);
    uint2 b = Input.Load2(offset + 16);

    Output.Store4(offset, a ^ uint4(b.x, b.y, b.x, b.y));
    Output.Store(offset + 16, a.x + b.y);

    uint lo = a.y;
    uint hi = a.z;
    Output.Store(offset + 20, (lo >> 7) | (hi << 25));
    Output.Store(offset + 24, (a.x * 2654435761u) ^ b.x);
}
