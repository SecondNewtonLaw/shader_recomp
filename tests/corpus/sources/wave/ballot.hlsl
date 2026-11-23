// Wave ballot, vote and lane queries.

RWStructuredBuffer<uint> Output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint lane = WaveGetLaneIndex();
    uint count = WaveGetLaneCount();
    bool predicate = (tid.x & 1u) != 0u;

    uint4 ballot = WaveActiveBallot(predicate);

    uint base = tid.x * 8;
    Output[base + 0] = ballot.x;
    Output[base + 1] = ballot.y;
    Output[base + 2] = ballot.z;
    Output[base + 3] = ballot.w;
    Output[base + 4] = WaveActiveAllTrue(predicate) ? 1u : 0u;
    Output[base + 5] = WaveActiveAnyTrue(predicate) ? 1u : 0u;
    Output[base + 6] = WaveActiveCountBits(predicate);
    Output[base + 7] = lane | (count << 8);
}
