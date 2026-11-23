// groupshared memory, barrier and transposed access.

#define TILE 16

groupshared float tile[TILE][TILE + 1];

Texture2D<float4> Input : register(t0);
RWStructuredBuffer<float4> Output : register(u0);

[numthreads(TILE, TILE, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID)
{
    uint2 p = gid.xy * TILE + gtid.xy;

    float4 v = Input.Load(int3(p, 0));
    tile[gtid.y][gtid.x] = v.x + v.y;

    GroupMemoryBarrierWithGroupSync();

    float s = tile[gtid.x][gtid.y] + tile[TILE - 1 - gtid.y][TILE - 1 - gtid.x];
    Output[p.y * 64 + p.x] = float4(s, v.z, v.w, 1.0);
}
