// Wave-scope packed integer matrix multiply.

#include <dx/linalg.h>

using namespace dx::linalg;

ByteAddressBuffer MatrixA : register(t0);
ByteAddressBuffer MatrixB : register(t1);
RWByteAddressBuffer MatrixC : register(u0);

#define M 16
#define N 16
#define K 32

[numthreads(32, 1, 1)]
void main()
{
    using MatrixATy = Matrix<ComponentType::I8, M, K, MatrixUse::A, MatrixScope::Wave>;
    using MatrixBTy = Matrix<ComponentType::U8, K, N, MatrixUse::B, MatrixScope::Wave>;
    using MatrixCTy = Matrix<ComponentType::I32, M, N, MatrixUse::Accumulator, MatrixScope::Wave>;

    MatrixCTy accumulator = MatrixCTy::Splat(0);

    MatrixATy a = MatrixATy::Load(MatrixA, 0, K, MatrixLayout::RowMajor);
    MatrixBTy b = MatrixBTy::Load(MatrixB, 0, N, MatrixLayout::RowMajor);

    accumulator.MultiplyAccumulate(a, b);

    accumulator.Store(MatrixC, 0, N * 4, MatrixLayout::RowMajor);
}
