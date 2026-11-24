#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

#include "SR/Common/IL/Attributes.hpp"
#include "SR/Common/IL/Signature.hpp"
#include "SR/Common/IL/Types.hpp"

namespace SR::IL {

enum class IntrinsicId : std::uint16_t
{
    None = 0,
    WaveBallot,
    WaveVote,
    WaveShuffle,
    WavePermute,
    WaveMatch,
    WaveReduce,
    WavePrefix,
    WaveLeader,
    WaveBroadcastFirst,
    WaveActiveMask,
    MaskBit,
    WaveLaneId,
    Derivative,
    Quad,
    TextureSample,
    TextureStore,
    ImageAtomic,
    Mma,
    TensorLoad,
    TensorStore,
    RayQuery,
    Send,
    SpecialReg,
    Transcendental,
    Division,
    DotProduct,
    Sad,
    Average,
    Bitfield,
    BytePermute,
    StochasticRound,
    Compress,
    Decompress,
    Barrier,
    Fence,
    AsyncCopy,
    AsyncWait,
    ResourceFromValue,
    Sleep,
    Yield,
    DebugBreak,
    Trap,
    TestBufferStore,
    TestBufferLoad,
    LocalStore,
    LocalLoad,
    StageInput,
    StageOutput,
    UserData,
    DeviceLoad,
    DeviceStore,
    ImageLoad,
    ImageStore,
    ImageSample,
};

enum class SpecialRegId : std::uint16_t
{
    ThreadIdX,
    ThreadIdY,
    ThreadIdZ,
    WorkgroupId,
    WorkgroupSize,
    LocalInvocationId,
    GlobalInvocationId,
    GlobalOffset,
    NumWorkgroups,
    SubgroupLocalInvocationId,
    SubgroupSize,
    NumSubgroups,
    SubgroupId,
    Clock,
    FlatScratchBase,
    PrivateBase,
    SharedBase,
    DispatchPtr,
    QueuePtr,
    KernargPtr,
    ExecMaskLow,
    ExecMaskHigh,
};

enum class MathFn : std::uint8_t
{
    Rcp,
    Rsq,
    Exp,
    Exp2,
    Log,
    Log2,
    Sin,
    Cos,
    Tanh,
    Atan2,
    Sqrt,
};

enum class VoteMode : std::uint8_t
{
    Any,
    All,
    Uniform,
};

enum class ShuffleMode : std::uint8_t
{
    Index,
    Up,
    Down,
    Xor,
    Butterfly,
};

enum class DerivativeMode : std::uint8_t
{
    Ddx,
    Ddy,
    DdxFine,
    DdyFine,
    DdxCoarse,
    DdyCoarse,
    Fwidth,
    FwidthFine,
    FwidthCoarse,
};

enum class ReduceOp : std::uint8_t
{
    Add,
    Mul,
    Min,
    Max,
    And,
    Or,
    Xor,
};

struct SpecialRegPayload
{
    SpecialRegId id = SpecialRegId::ThreadIdX;
};

struct VotePayload
{
    VoteMode mode = VoteMode::Any;
};

struct ShufflePayload
{
    ShuffleMode mode = ShuffleMode::Index;
    std::uint32_t width = 0; // 0 = natural subgroup width
};

struct BarrierPayload
{
    MemoryScope scope = MemoryScope::Workgroup;
    MemoryOrder order = MemoryOrder::Release;
    std::uint32_t count = 0; // 0 = all invocations
};

struct FencePayload
{
    MemoryScope scope = MemoryScope::Device;
    MemoryOrder order = MemoryOrder::Release;
};

struct SampleDesc
{
    std::uint8_t dimensions = 2;
    bool arrayed = false;
    bool shadow = false;
    bool gather = false;
    bool implicitLod = false;
    std::uint8_t components = 4;
};

struct DotDesc
{
    std::uint8_t inputBits = 8;
    std::uint8_t elements = 4;
    bool accumulate = false;
    bool signedInput = true;
};

struct SendDesc
{
    std::uint32_t message = 0;
    bool known = false;
};

struct TestBufferDesc
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
};

// One dword of the user data block the dispatch or draw supplies.
struct UserDataDesc
{
    std::uint32_t index = 0;
};

// One component of an image texel. The source names the surface through a T#
// descriptor; the host binds the image the descriptor resolved to, so the
// access carries the binding and the component index.
struct ImageAccessDesc
{
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t samplerBinding = 0;
    std::uint8_t component = 0;
    std::uint8_t dimensions = 2;
    // A sampled image is read through a sampler (normalized float coordinates);
    // a storage image is read at integer texel coordinates.
    bool sampled = false;
};

using Payload = std::variant<std::monostate,
                             SpecialRegPayload,
                             VotePayload,
                             ShufflePayload,
                             BarrierPayload,
                             FencePayload,
                             SampleDesc,
                             DotDesc,
                             SendDesc,
                             TestBufferDesc,
                             StageIoDesc,
                             UserDataDesc,
                             ImageAccessDesc>;

struct IntrinsicSpec
{
    IntrinsicId id = IntrinsicId::None;
    std::string_view name;
    std::uint8_t minOperands = 0;
    std::uint8_t maxOperands = 0;
    std::uint8_t minResults = 0;
    std::uint8_t maxResults = 0;
    Effect effects = Effect::None;
    bool requiresUniformControlFlow = false;
};

[[nodiscard]] const IntrinsicSpec& GetIntrinsicSpec(IntrinsicId id) noexcept;
[[nodiscard]] std::string_view ToString(IntrinsicId id) noexcept;
[[nodiscard]] std::string_view ToString(SpecialRegId id) noexcept;
[[nodiscard]] std::string_view ToString(MathFn fn) noexcept;

} // namespace SR::IL
