#include "SR/Frontend/MasterReader.hpp"

#include <utility>

#include "SR/Frontend/Readers/CubinReader.hpp"
#include "SR/Frontend/Readers/DxbcReader.hpp"
#include "SR/Frontend/Readers/DxilReader.hpp"
#include "SR/Frontend/Readers/HsacoReader.hpp"
#include "SR/Frontend/Readers/SpirvReader.hpp"

namespace SR::Frontend {

MasterReader::MasterReader()
{
    // DXIL lives in a DXBC container, so it is probed before DXBC.
    Add(std::make_unique<SpirvReader>());
    Add(std::make_unique<DxilReader>());
    Add(std::make_unique<DxbcReader>());
    Add(std::make_unique<CubinReader>());
    Add(std::make_unique<HsacoReader>());
}

void MasterReader::Add(std::unique_ptr<IBinaryReader> reader)
{
    if (reader)
        m_readers.push_back(std::move(reader));
}

Result<BinaryFormat> MasterReader::Detect(std::span<const std::uint8_t> bytes) const
{
    for (const auto& reader : m_readers)
    {
        if (reader->Probe(bytes))
            return reader->Format();
    }

    return std::unexpected(Diagnostic::Error(
        "SR-FRONTEND-001",
        "unrecognized shader binary: no registered reader matched the header"));
}

Result<DecodedShader> MasterReader::Read(std::span<const std::uint8_t> bytes) const
{
    for (const auto& reader : m_readers)
    {
        if (!reader->Probe(bytes))
            continue;

        return reader->Read(bytes);
    }

    return std::unexpected(Diagnostic::Error(
        "SR-FRONTEND-002",
        "unrecognized shader binary: no registered reader matched the header"));
}

std::vector<std::string_view> MasterReader::ReaderNames() const
{
    std::vector<std::string_view> names;
    names.reserve(m_readers.size());
    for (const auto& reader : m_readers)
        names.push_back(reader->Name());
    return names;
}

} // namespace SR::Frontend
