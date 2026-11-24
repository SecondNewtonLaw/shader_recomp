#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Common/IL/Module.hpp"

namespace SR::Frontend {

// Hardware stages in PAL order, as the console records them. A vertex stage that
// also feeds a geometry stage ships twice: once as ES (the merged pair) and once
// as VS (standalone). Mesh programs use the NGG record.
enum class XboxStage : std::uint8_t
{
    Ls,
    Hs,
    Es,
    Gs,
    Vs,
    Ps,
    Cs,
    Mesh,
    Unknown,
};

[[nodiscard]] std::string_view ToString(XboxStage stage) noexcept;

// One native program inside a console container. The code lives in the
// decompressed FSSM stream, not in the container bytes, so it is addressed as
// (stream, offset).
struct XboxProgram
{
    XboxStage stage = XboxStage::Unknown;
    std::uint32_t stream = 0;
    std::uint32_t codeOffset = 0;
    std::uint32_t codeSize = 0;
    bool wave32 = false;
    std::uint32_t vgprCount = 0;
    std::uint32_t sgprCount = 0;
    std::uint32_t ldsBytes = 0;
    std::array<std::uint32_t, 3> threadGroupSize{1, 1, 1};
    std::uint32_t computeRsrc2 = 0;
    bool hasComputeState = false;
};

// An Xbox Series X|S shader: a DXBC container whose DXIL part carries an XDAT
// index, an XBCH zlib stream and FSSM machine code instead of LLVM bitcode.
//
// The outer container is a normal DXBC container, so the signature parts
// (ISG1/OSG1/PSG1/PSV0) remain the authority on attribute rows. Two published
// descriptions of the FSSM payload disagree on where a stage's code pointer
// sits; this parser validates the record table first, falls back to the direct
// pointer, and records which one matched in notes(). A container where neither
// validates is an error, never a guess.
class XboxShaderContainer
{
public:
    [[nodiscard]] static bool Probe(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] static Result<XboxShaderContainer> Parse(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
    [[nodiscard]] IL::Stage stage() const noexcept;
    [[nodiscard]] std::uint16_t shaderModelMajor() const noexcept;
    [[nodiscard]] std::uint16_t shaderModelMinor() const noexcept;
    [[nodiscard]] std::uint32_t fssmVersion() const noexcept;

    [[nodiscard]] const std::vector<XboxProgram>& programs() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> code(const XboxProgram& program) const noexcept;

    [[nodiscard]] std::span<const std::uint8_t> hash() const noexcept;
    [[nodiscard]] const std::vector<std::string>& notes() const noexcept;

private:
    std::span<const std::uint8_t> m_blob;
    std::vector<std::vector<std::uint8_t>> m_streams; // decompressed FSSM programs
    std::vector<XboxProgram> m_programs;
    std::vector<std::string> m_notes;
    IL::Stage m_stage = IL::Stage::Unknown;
    std::uint16_t m_shaderModelMajor = 0;
    std::uint16_t m_shaderModelMinor = 0;
    std::uint32_t m_fssmVersion = 0;
    std::uint32_t m_hashOffset = 0;
    std::uint32_t m_hashSize = 0;
};

} // namespace SR::Frontend
