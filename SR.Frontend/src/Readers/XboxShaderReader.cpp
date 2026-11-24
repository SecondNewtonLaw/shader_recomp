#include "SR/Frontend/Readers/XboxShaderReader.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "SR/Frontend/Container/XboxShaderContainer.hpp"

namespace SR::Frontend {
namespace {

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

// The stage's program in the order it should be preferred. A vertex stage that
// feeds a geometry stage ships as an ES/VS pair; the standalone VS is the one a
// host graphics pipeline wants.
constexpr XboxStage kVertexStages[] = {XboxStage::Vs, XboxStage::Es};
constexpr XboxStage kTessControlStages[] = {XboxStage::Hs};
constexpr XboxStage kTessEvalStages[] = {XboxStage::Ls};
constexpr XboxStage kGeometryStages[] = {XboxStage::Gs};
constexpr XboxStage kFragmentStages[] = {XboxStage::Ps};
constexpr XboxStage kComputeStages[] = {XboxStage::Cs};
constexpr XboxStage kMeshStages[] = {XboxStage::Mesh};

std::span<const XboxStage> PreferredStages(IL::Stage stage) noexcept
{
    switch (stage)
    {
    case IL::Stage::Vertex:
        return kVertexStages;
    case IL::Stage::TessControl:
        return kTessControlStages;
    case IL::Stage::TessEval:
        return kTessEvalStages;
    case IL::Stage::Geometry:
        return kGeometryStages;
    case IL::Stage::Fragment:
        return kFragmentStages;
    case IL::Stage::Compute:
        return kComputeStages;
    case IL::Stage::Mesh:
        return kMeshStages;
    default:
        return {};
    }
}

} // namespace

BinaryFormat XboxShaderReader::Format() const
{
    return BinaryFormat::XboxShader;
}

std::string_view XboxShaderReader::Name() const
{
    return "xbox";
}

bool XboxShaderReader::Probe(std::span<const std::uint8_t> bytes) const
{
    return XboxShaderContainer::Probe(bytes);
}

Result<DecodedShader> XboxShaderReader::Read(std::span<const std::uint8_t> bytes) const
{
    const auto container = XboxShaderContainer::Parse(bytes);
    if (!container)
        return std::unexpected(container.error());

    const auto& programs = container->programs();
    const XboxProgram* chosen = nullptr;

    for (const XboxStage stage : PreferredStages(container->stage()))
    {
        const auto match = std::find_if(programs.begin(), programs.end(), [stage](const XboxProgram& program) {
            return program.stage == stage;
        });

        if (match != programs.end())
        {
            chosen = &*match;
            break;
        }
    }

    // A single-program container is unambiguous even when the DXIL kind is not a
    // stage this front end names yet.
    if (chosen == nullptr && programs.size() == 1)
        chosen = &programs.front();

    if (chosen == nullptr)
        return std::unexpected(MakeError(
            "SR-XBOX-020",
            "container has no program for stage " + std::string(IL::ToString(container->stage())),
            0));

    const auto code = container->code(*chosen);
    if (code.empty())
        return std::unexpected(MakeError("SR-XBOX-021", "selected program's code range is invalid", 0));

    DecodedShader shader;
    shader.format = BinaryFormat::XboxShader;
    shader.stage = container->stage();
    shader.entryPoint = "main";
    shader.architecture = "gfx10.3-xbox";
    shader.shaderModel = static_cast<std::uint32_t>(container->shaderModelMajor()) << 4
                       | static_cast<std::uint32_t>(container->shaderModelMinor());
    shader.rawBytecode.assign(code.begin(), code.end());
    shader.notes = container->notes();

    return shader;
}

} // namespace SR::Frontend
