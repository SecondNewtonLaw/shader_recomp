#pragma once

#include <memory>
#include <span>
#include <string_view>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Common/IL/Module.hpp"
#include "SR/Frontend/BinaryFormat.hpp"
#include "SR/Frontend/DecodedShader.hpp"

namespace SR::Frontend {

class IBinaryReader
{
public:
    virtual ~IBinaryReader() = default;

    [[nodiscard]] virtual BinaryFormat Format() const = 0;
    [[nodiscard]] virtual std::string_view Name() const = 0;
    [[nodiscard]] virtual bool Probe(std::span<const std::uint8_t> bytes) const = 0;
    [[nodiscard]] virtual Result<DecodedShader> Read(std::span<const std::uint8_t> bytes) const = 0;
};

class IIsaRemapper
{
public:
    virtual ~IIsaRemapper() = default;

    [[nodiscard]] virtual Result<IL::Module> Remap(const DecodedShader& shader) const = 0;
};

struct Frontend
{
    std::string_view name;
    std::unique_ptr<IBinaryReader> reader;
    std::unique_ptr<IIsaRemapper> remapper;
};

[[nodiscard]] std::string_view LibraryVersion();

} // namespace SR::Frontend
