#pragma once

#include "SR/Frontend/Frontend.hpp"

namespace SR::Frontend {

class DxilReader final : public IBinaryReader
{
public:
    [[nodiscard]] BinaryFormat Format() const override;
    [[nodiscard]] std::string_view Name() const override;
    [[nodiscard]] bool Probe(std::span<const std::uint8_t> bytes) const override;
    [[nodiscard]] Result<DecodedShader> Read(std::span<const std::uint8_t> bytes) const override;
};

} // namespace SR::Frontend
