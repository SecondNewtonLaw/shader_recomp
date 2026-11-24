#pragma once

#include <cstdint>

#include "SR/Common/IL/Types.hpp"

namespace SR::IL {

// How the fixed-function stage produces an input value. The vendor's
// interpolation instructions (v_interp_*) and the console's SPI input table
// both describe this; the emitter turns it into an SPIR-V decoration and lets
// the host interpolate.
enum class Interpolation : std::uint8_t
{
    None,
    Perspective,
    Linear,
    NoPerspective,
    Flat,
    Centroid,
    Sample,
};

enum class Builtin : std::uint8_t
{
    None,
    Position,
    PointSize,
    VertexIndex,
    InstanceIndex,
    FragCoord,
    FragDepth,
    FrontFacing,
    PrimitiveId,
    Layer,
    ViewportIndex,
    TessCoord,
    BaryCoord,
    SampleId,
    SampleMask,
    ClipDistance,
    CullDistance,
    LocalInvocationId,
    GlobalInvocationId,
    WorkgroupId,
    LocalInvocationIndex,
    SubgroupInvocationId,
    SubgroupSize,
    NumWorkgroups,
};

// One stage input or output location. `location`/`component` name a signature
// row; `builtin` overrides them when the value is a system value. The payload
// is shared by the StageInput and StageOutput intrinsics.
struct StageIoDesc
{
    Space space = Space::Input;
    std::uint32_t location = 0;
    std::uint32_t component = 0;
    Builtin builtin = Builtin::None;
    Interpolation interpolation = Interpolation::None;
};

} // namespace SR::IL
