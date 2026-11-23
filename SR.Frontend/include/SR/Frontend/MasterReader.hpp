#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Frontend/DecodedShader.hpp"
#include "SR/Frontend/Frontend.hpp"

namespace SR::Frontend {

class MasterReader
{
public:
    MasterReader();

    void Add(std::unique_ptr<IBinaryReader> reader);

    [[nodiscard]] Result<BinaryFormat> Detect(std::span<const std::uint8_t> bytes) const;
    [[nodiscard]] Result<DecodedShader> Read(std::span<const std::uint8_t> bytes) const;
    [[nodiscard]] std::vector<std::string_view> ReaderNames() const;

private:
    std::vector<std::unique_ptr<IBinaryReader>> m_readers;
};

} // namespace SR::Frontend
