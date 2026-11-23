// Vertex shader with constant buffers and matrix transforms.

cbuffer Transforms : register(b0)
{
    float4x4 world;
    float4x4 viewProjection;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPosition = mul(float4(input.position, 1.0), world);

    output.position = mul(worldPosition, viewProjection);
    output.normal = normalize(mul(float4(input.normal, 0.0), world).xyz);
    output.uv = input.uv;

    return output;
}
