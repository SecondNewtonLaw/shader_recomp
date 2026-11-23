// Thread-group-scope matrix multiply.

#include <dx/linalg.h>

using namespace dx::linalg;

ByteAddressBuffer MatrixA : register(t0);
ByteAddressBuffer MatrixB : register(t1);
RWByteAddressBuffer MatrixC : register(u0);

#define M 32
#define N 32
#define K 16

[numthreads(128, 1, 1)]
void main()
{
    using MatrixATy = Matrix<ComponentType::F16, M, K, MatrixUse::A, MatrixScope::ThreadGroup>;
    using MatrixBTy = Matrix<ComponentType::F16, K, N, MatrixUse::B, MatrixScope::ThreadGroup>;
    using MatrixCTy = Matrix<ComponentType::F32, M, N, MatrixUse::Accumulator, MatrixScope::ThreadGroup>;

    MatrixATy a = MatrixATy::Load(MatrixA, 0, K * 2, MatrixLayout::RowMajor);
    MatrixBTy b = MatrixBTy::Load(MatrixB, 0, N * 2, MatrixLayout::RowMajor);

    MatrixCTy c = Multiply<ComponentType::F32>(a, b);

    c.Store(MatrixC, 0, N * 4, MatrixLayout::RowMajor);
}
