// Wave reductions and prefix operations.

RWStructuredBuffer<float> Output : register(u0);

[numthreads(64, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    float value = (float)(tid.x + 1);

    float sum = WaveActiveSum(value);
    float product = WaveActiveProduct(value);
    float minimum = WaveActiveMin(value);
    float maximum = WaveActiveMax(value);
    float prefix = WavePrefixSum(value);
    uint bits = WaveActiveCountBits(true);

    uint base = tid.x * 6;
    Output[base + 0] = sum;
    Output[base + 1] = product;
    Output[base + 2] = minimum;
    Output[base + 3] = maximum;
    Output[base + 4] = prefix;
    Output[base + 5] = (float)bits;
}
