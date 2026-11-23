// Wave lane reads and writes.

RWStructuredBuffer<float> Output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint lane = WaveGetLaneIndex();
    uint count = WaveGetLaneCount();
    float value = (float)(tid.x + 1);

    float next = WaveReadLaneAt(value, (lane + 1) % count);
    float previous = WaveReadLaneAt(value, (lane + count - 1) % count);
    float first = WaveReadLaneFirst(value);

    uint active = WaveActiveBallot(true).x;
    uint firstBit = firstbitlow(active);
    float firstActive = WaveReadLaneAt(value, firstBit);

    Output[tid.x * 4 + 0] = next;
    Output[tid.x * 4 + 1] = previous;
    Output[tid.x * 4 + 2] = first;
    Output[tid.x * 4 + 3] = firstActive;
}
