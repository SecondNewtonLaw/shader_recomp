// Amplification shader dispatching a mesh.

struct Payload
{
    uint meshletCount;
};

[numthreads(32, 1, 1)]
void main(uint3 gid : SV_GroupID)
{
    Payload payload;
    payload.meshletCount = 1;

    DispatchMesh(payload.meshletCount, 1, 1, payload);
}
