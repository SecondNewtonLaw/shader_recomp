#pragma once

#include "SR/Frontend/Frontend.hpp"

namespace SR::Frontend {

// Reads Xbox Series X|S shader containers: DXBC whose DXIL part carries native
// GFX10.3 machine code in an XBCH/FSSM stream. The selected stage's code becomes
// the decoded shader's raw bytecode, so the GFX lifter consumes it unchanged.
class XboxShaderReader final : public IBinaryReader
{
public:
    [[nodiscard]] BinaryFormat Format() const override;
    [[nodiscard]] std::string_view Name() const override;
    [[nodiscard]] bool Probe(std::span<const std::uint8_t> bytes) const override;
    [[nodiscard]] Result<DecodedShader> Read(std::span<const std::uint8_t> bytes) const override;
};

} // namespace SR::Frontend
