#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "SR/Common/IL/Module.hpp"
#include "SR/Frontend/Arch/Gfx/GfxIsa.hpp"
#include "SR/Frontend/Frontend.hpp"

namespace SR::Frontend::Gfx {

struct LiftOptions
{
    LiftOptions() = default;
    explicit LiftOptions(bool testAbiValue)
        : testAbi(testAbiValue)
    {
    }

    // Treat global stores as writes to the test buffer at set 0, binding 0.
    bool testAbi = false;
    // Wave width of the source program: a compare mask occupies two SGPRs on a
    // 64-lane wave and one on a 32-lane wave.
    bool wave64 = false;
    // User data the dispatch or draw supplies in the first scalar registers:
    // root constants, descriptor table pointers and root descriptor addresses,
    // in root order.
    std::vector<std::uint32_t> userData;
    // Seed the user data from the host's push constants instead of folding the
    // values above into the module.
    bool userDataFromPush = false;
    // A geometry stage's topology and vertex budget are pipeline state, not
    // instructions, so they arrive here rather than from the code object.
    IL::InputTopology geometryInputTopology = IL::InputTopology::Triangles;
    IL::OutputTopology geometryOutputTopology = IL::OutputTopology::TriangleStrip;
    std::uint32_t geometryOutputVertices = 3;
    // A geometry shader that stages its vertices in local memory names one
    // four-component register per vertex; the count sizes the staging area and
    // the slot stride, so it is required for that path.
    std::uint32_t geometryOutputRegisters = 0;
    // SPIR-V image format of the surfaces an atomic writes (33 = R32ui, 0 =
    // unknown). The descriptor format is not decoded yet, so the pipeline
    // state has to state it; an atomic without one is refused.
    std::uint8_t imageFormat = 0;
};

// Lifts a code object into COMMON_SHADER. Control flow and effects are
// diagnosed until the corresponding passes exist.
[[nodiscard]] Result<IL::Module> Lift(const DecodedShader& shader,
                                      Arch arch,
                                      std::string_view entryPoint = "main",
                                      LiftOptions options = {});

class GfxRemapper final : public IIsaRemapper
{
public:
    explicit GfxRemapper(Arch arch, LiftOptions options = {})
        : m_arch(arch)
        , m_options(options)
    {
    }

    [[nodiscard]] Result<IL::Module> Remap(const DecodedShader& shader) const override
    {
        return Lift(shader, m_arch, "main", m_options);
    }

    [[nodiscard]] Arch ArchTarget() const noexcept { return m_arch; }

private:
    Arch m_arch;
    LiftOptions m_options;
};

} // namespace SR::Frontend::Gfx
