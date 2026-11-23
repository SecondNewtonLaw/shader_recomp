// Geometry shader with a triangle stream.

struct GSInput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

struct GSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
};

[maxvertexcount(3)]
void main(triangle GSInput input[3], inout TriangleStream<GSOutput> stream)
{
    for (uint i = 0; i < 3; ++i)
    {
        GSOutput output;
        output.position = input[i].position;
        output.normal = input[i].normal;
        stream.Append(output);
    }

    stream.RestartStrip();
}
