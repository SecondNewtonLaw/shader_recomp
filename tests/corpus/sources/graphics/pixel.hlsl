// Pixel shader with lighting, texture sampling and clip.

Texture2D<float4> Albedo : register(t0);
SamplerState LinearSampler : register(s0);

float4 main(float4 position : SV_Position, float3 normal : TEXCOORD0, float2 uv : TEXCOORD1) : SV_Target
{
    float3 lightDirection = normalize(float3(1.0, 2.0, 3.0));
    float diffuse = saturate(dot(normalize(normal), lightDirection));

    float4 color = Albedo.Sample(LinearSampler, uv);
    clip(color.a - 0.1);

    return float4(color.rgb * diffuse, color.a);
}
