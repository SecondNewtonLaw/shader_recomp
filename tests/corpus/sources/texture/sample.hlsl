// Texture sampling with explicit and implicit derivatives.

Texture2D<float4> Texture : register(t0);
SamplerState LinearSampler : register(s0);

cbuffer Params : register(b0)
{
    float2 uvScale;
    float lod;
};

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
    float2 st = uv * uvScale;

    float4 implicitLod = Texture.Sample(LinearSampler, st);
    float4 explicitLod = Texture.SampleLevel(LinearSampler, st, lod);
    float4 gradient = Texture.SampleGrad(LinearSampler, st, ddx(st), ddy(st));
    float4 biased = Texture.SampleBias(LinearSampler, st, 0.5);

    return implicitLod + explicitLod + gradient + biased;
}
