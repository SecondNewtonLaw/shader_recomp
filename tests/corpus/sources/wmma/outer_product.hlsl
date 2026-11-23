// Thread-scope outer product with an atomic accumulate.

#include <dx/linalg.h>

using namespace dx::linalg;

RWByteAddressBuffer Output : register(u0);

[numthreads(32, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    using MatrixTy = Matrix<ComponentType::F16, 16, 16, MatrixUse::Accumulator, MatrixScope::Thread>;

    vector<float16_t, 16> a = (vector<float16_t, 16>)0;
    vector<float16_t, 16> b = (vector<float16_t, 16>)0;

    a[tid.x & 15] = (float16_t)1.0;
    b[(tid.x + 1) & 15] = (float16_t)2.0;

    MatrixTy m = OuterProduct<ComponentType::F16>(a, b);
    m.InterlockedAccumulate(Output, 0);
}
