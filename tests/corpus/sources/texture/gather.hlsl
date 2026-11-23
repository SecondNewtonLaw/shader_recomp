// Texture gather and comparison sampling.

Texture2D<float4> Texture : register(t0);
Texture2D<float> ShadowMap : register(t1);
SamplerState LinearSampler : register(s0);
SamplerComparisonState ComparisonSampler : register(s1);

float4 main(float2 uv : TEXCOORD0) : SV_Target
{
    float4 gather = Texture.Gather(LinearSampler, uv);
    float4 gatherRed = Texture.GatherRed(LinearSampler, uv, int2(1, 1));

    float shadow = ShadowMap.SampleCmpLevelZero(ComparisonSampler, uv, 0.5);
    float2 size;
    ShadowMap.GetDimensions(size.x, size.y);

    return gather + gatherRed + float4(shadow, size.x, size.y, 0.0);
}
