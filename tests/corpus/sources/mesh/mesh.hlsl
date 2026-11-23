// Mesh shader with a generated triangle strip.

struct Vertex
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void main(out vertices Vertex vertices[64], out indices uint3 triangles[32], uint3 gtid : SV_GroupThreadID)
{
    SetMeshOutputCounts(64, 32);

    if (gtid.x < 64)
    {
        float angle = (float)gtid.x * 0.09817477;
        vertices[gtid.x].position = float4(cos(angle), sin(angle), (float)(gtid.x & 1), 1.0);
        vertices[gtid.x].normal = float3(0.0, 0.0, 1.0);
    }

    if (gtid.x < 32)
        triangles[gtid.x] = uint3(gtid.x * 2, gtid.x * 2 + 1, gtid.x * 2 + 2);
}
