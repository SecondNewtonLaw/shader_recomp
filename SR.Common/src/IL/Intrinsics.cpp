#include "SR/Common/IL/Intrinsics.hpp"

#include <array>

namespace SR::IL {
namespace {

constexpr IntrinsicSpec Make(IntrinsicId id,
                             std::string_view name,
                             std::uint8_t minOperands,
                             std::uint8_t maxOperands,
                             std::uint8_t minResults,
                             std::uint8_t maxResults,
                             Effect effects,
                             bool uniform = false) noexcept
{
    return IntrinsicSpec{id, name, minOperands, maxOperands, minResults, maxResults, effects, uniform};
}

constexpr std::array<IntrinsicSpec, 49> kSpecs = {
    Make(IntrinsicId::None, "none", 0, 0, 0, 0, Effect::None),
    Make(IntrinsicId::WaveBallot, "wave.ballot", 1, 1, 1, 1, Effect::Wave),
    Make(IntrinsicId::WaveVote, "wave.vote", 1, 1, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WaveShuffle, "wave.shuffle", 2, 2, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WavePermute, "wave.permute", 2, 2, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WaveMatch, "wave.match", 1, 1, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WaveReduce, "wave.reduce", 1, 1, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WavePrefix, "wave.prefix", 1, 1, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WaveLeader, "wave.leader", 0, 0, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WaveBroadcastFirst, "wave.broadcast-first", 1, 1, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::WaveActiveMask, "wave.active-mask", 0, 0, 1, 1, Effect::Wave, true),
    Make(IntrinsicId::MaskBit, "mask.bit", 1, 1, 1, 1, Effect::None),
    Make(IntrinsicId::WaveLaneId, "wave.lane-id", 0, 0, 1, 1, Effect::None),
    Make(IntrinsicId::Derivative, "derivative", 1, 1, 1, 1, Effect::None),
    Make(IntrinsicId::Quad, "quad", 1, 1, 1, 1, Effect::Wave),
    Make(IntrinsicId::TextureSample, "texture.sample", 2, 8, 1, 1, Effect::ReadMemory),
    Make(IntrinsicId::TextureStore, "texture.store", 2, 8, 0, 0, Effect::WriteMemory),
    Make(IntrinsicId::ImageAtomic, "texture.atomic", 3, 5, 0, 1, Effect::ReadMemory | Effect::WriteMemory | Effect::Atomic),
    Make(IntrinsicId::Mma, "mma", 3, 4, 1, 1, Effect::None),
    Make(IntrinsicId::TensorLoad, "tensor.load", 1, 2, 1, 1, Effect::ReadMemory),
    Make(IntrinsicId::TensorStore, "tensor.store", 2, 2, 0, 0, Effect::WriteMemory),
    Make(IntrinsicId::RayQuery, "ray.query", 2, 4, 0, 1, Effect::ReadMemory),
    Make(IntrinsicId::Send, "send", 1, 4, 0, 2, Effect::ReadMemory | Effect::WriteMemory),
    Make(IntrinsicId::SpecialReg, "special", 0, 0, 1, 1, Effect::None),
    Make(IntrinsicId::Transcendental, "transcendental", 1, 1, 1, 1, Effect::None),
    Make(IntrinsicId::Division, "division", 2, 2, 1, 1, Effect::None),
    Make(IntrinsicId::DotProduct, "dot", 2, 3, 1, 1, Effect::None),
    Make(IntrinsicId::Sad, "sad", 2, 2, 1, 1, Effect::None),
    Make(IntrinsicId::Average, "average", 2, 2, 1, 1, Effect::None),
    Make(IntrinsicId::Bitfield, "bitfield", 1, 3, 1, 1, Effect::None),
    Make(IntrinsicId::BytePermute, "byte-permute", 3, 3, 1, 1, Effect::None),
    Make(IntrinsicId::StochasticRound, "stochastic-round", 1, 1, 1, 1, Effect::None),
    Make(IntrinsicId::Compress, "compress", 1, 1, 1, 1, Effect::None),
    Make(IntrinsicId::Decompress, "decompress", 1, 1, 1, 1, Effect::None),
    Make(IntrinsicId::Barrier, "barrier", 0, 0, 0, 0, Effect::Barrier, true),
    Make(IntrinsicId::Fence, "fence", 0, 0, 0, 0, Effect::None, false),
    Make(IntrinsicId::AsyncCopy, "async.copy", 2, 4, 0, 1, Effect::ReadMemory | Effect::WriteMemory),
    Make(IntrinsicId::AsyncWait, "async.wait", 0, 1, 0, 0, Effect::None, true),
    Make(IntrinsicId::ResourceFromValue, "resource.from-value", 1, 1, 1, 1, Effect::None),
    Make(IntrinsicId::Sleep, "sleep", 0, 1, 0, 0, Effect::None),
    Make(IntrinsicId::Yield, "yield", 0, 0, 0, 0, Effect::None),
    Make(IntrinsicId::DebugBreak, "debug.break", 0, 0, 0, 0, Effect::Trap),
    Make(IntrinsicId::Trap, "trap", 0, 0, 0, 0, Effect::Trap),
    Make(IntrinsicId::TestBufferStore, "test.buffer-store", 2, 2, 0, 0, Effect::WriteMemory),
    Make(IntrinsicId::TestBufferLoad, "test.buffer-load", 1, 1, 1, 1, Effect::ReadMemory),
    Make(IntrinsicId::LocalStore, "local.store", 2, 2, 0, 0, Effect::WriteMemory),
    Make(IntrinsicId::LocalLoad, "local.load", 1, 1, 1, 1, Effect::ReadMemory),
    Make(IntrinsicId::StageInput, "stage.input", 0, 0, 1, 1, Effect::None),
    Make(IntrinsicId::StageOutput, "stage.output", 1, 1, 0, 0, Effect::WriteMemory),
};

} // namespace

const IntrinsicSpec& GetIntrinsicSpec(IntrinsicId id) noexcept
{
    const auto index = static_cast<std::size_t>(id);
    return kSpecs[index < kSpecs.size() ? index : 0];
}

std::string_view ToString(IntrinsicId id) noexcept
{
    return GetIntrinsicSpec(id).name;
}

std::string_view ToString(SpecialRegId id) noexcept
{
    switch (id)
    {
    case SpecialRegId::ThreadIdX:
        return "thread-id-x";
    case SpecialRegId::ThreadIdY:
        return "thread-id-y";
    case SpecialRegId::ThreadIdZ:
        return "thread-id-z";
    case SpecialRegId::WorkgroupId:
        return "workgroup-id";
    case SpecialRegId::WorkgroupSize:
        return "workgroup-size";
    case SpecialRegId::LocalInvocationId:
        return "local-invocation-id";
    case SpecialRegId::GlobalInvocationId:
        return "global-invocation-id";
    case SpecialRegId::GlobalOffset:
        return "global-offset";
    case SpecialRegId::NumWorkgroups:
        return "num-workgroups";
    case SpecialRegId::SubgroupLocalInvocationId:
        return "subgroup-local-invocation-id";
    case SpecialRegId::SubgroupSize:
        return "subgroup-size";
    case SpecialRegId::NumSubgroups:
        return "num-subgroups";
    case SpecialRegId::SubgroupId:
        return "subgroup-id";
    case SpecialRegId::Clock:
        return "clock";
    case SpecialRegId::FlatScratchBase:
        return "flat-scratch-base";
    case SpecialRegId::PrivateBase:
        return "private-base";
    case SpecialRegId::SharedBase:
        return "shared-base";
    case SpecialRegId::DispatchPtr:
        return "dispatch-ptr";
    case SpecialRegId::QueuePtr:
        return "queue-ptr";
    case SpecialRegId::KernargPtr:
        return "kernarg-ptr";
    case SpecialRegId::ExecMaskLow:
        return "exec-mask-low";
    case SpecialRegId::ExecMaskHigh:
        return "exec-mask-high";
    }

    return "unknown";
}

std::string_view ToString(MathFn fn) noexcept
{
    switch (fn)
    {
    case MathFn::Rcp:
        return "rcp";
    case MathFn::Rsq:
        return "rsq";
    case MathFn::Exp:
        return "exp";
    case MathFn::Exp2:
        return "exp2";
    case MathFn::Log:
        return "log";
    case MathFn::Log2:
        return "log2";
    case MathFn::Sin:
        return "sin";
    case MathFn::Cos:
        return "cos";
    case MathFn::Tanh:
        return "tanh";
    case MathFn::Atan2:
        return "atan2";
    case MathFn::Sqrt:
        return "sqrt";
    }

    return "unknown";
}

} // namespace SR::IL
