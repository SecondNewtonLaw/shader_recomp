// Wave-scope FP16 matrix multiply with FP32 accumulation.

#include <dx/linalg.h>

using namespace dx::linalg;

ByteAddressBuffer MatrixA : register(t0);
ByteAddressBuffer MatrixB : register(t1);
RWByteAddressBuffer MatrixC : register(u0);

#define TILE 16

[numthreads(TILE, 1, 1)]
void main(uint3 group : SV_GroupID)
{
    using MatrixATy = Matrix<ComponentType::F16, TILE, TILE, MatrixUse::A, MatrixScope::Wave>;
    using MatrixBTy = Matrix<ComponentType::F16, TILE, TILE, MatrixUse::B, MatrixScope::Wave>;
    using MatrixCTy = Matrix<ComponentType::F32, TILE, TILE, MatrixUse::Accumulator, MatrixScope::Wave>;

    MatrixCTy accumulator = MatrixCTy::Splat(0.0f);

    uint offsetA = (group.y * TILE) * TILE * 2;
    uint offsetB = (group.x * TILE) * TILE * 2;

    MatrixATy a = MatrixATy::Load(MatrixA, offsetA, TILE * 2, MatrixLayout::RowMajor);
    MatrixBTy b = MatrixBTy::Load(MatrixB, offsetB, TILE * 2, MatrixLayout::RowMajor);

    accumulator.MultiplyAccumulate(a, b);

    uint offsetC = (group.y * TILE * TILE + group.x * TILE) * 4;
    accumulator.Store(MatrixC, offsetC, TILE * 4, MatrixLayout::RowMajor);
}
