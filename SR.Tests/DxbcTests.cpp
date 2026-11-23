#include <cstdint>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SR/Frontend/Container/DxbcContainer.hpp"
#include "SR/Frontend/MasterReader.hpp"

namespace {

void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void AppendFourCC(std::vector<std::uint8_t>& bytes, const char (&fourcc)[5])
{
    for (int i = 0; i < 4; ++i)
        bytes.push_back(static_cast<std::uint8_t>(fourcc[i]));
}

std::vector<std::uint8_t> MakeMinimalDxbc()
{
    // Header (32) + one part offset (4) + part header (8) + program (12).
    std::vector<std::uint8_t> bytes;
    bytes.reserve(56);

    AppendFourCC(bytes, "DXBC");
    for (int i = 0; i < 16; ++i)
        bytes.push_back(0);

    AppendU16(bytes, 1); // container major
    AppendU16(bytes, 0); // container minor
    AppendU32(bytes, 56); // total size
    AppendU32(bytes, 1);  // part count
    AppendU32(bytes, 36); // part offset

    AppendFourCC(bytes, "SHEX");
    AppendU32(bytes, 12); // part size

    AppendU32(bytes, 0x00050050u); // SM 5.0 compute
    AppendU32(bytes, 3);           // program DWORDs including version + length
    AppendU32(bytes, 0x0100003eu); // ret

    return bytes;
}

} // namespace

TEST_CASE("DxbcContainer parses the header and parts", "[dxbc]")
{
    const auto bytes = MakeMinimalDxbc();
    const auto container = SR::Frontend::DxbcContainer::Parse(bytes);

    REQUIRE(container.has_value());
    REQUIRE(container->versionMajor() == 1);
    REQUIRE(container->versionMinor() == 0);
    REQUIRE(container->chunks().size() == 1);
    REQUIRE(container->IsTokenized());
    REQUIRE_FALSE(container->IsDxil());
    REQUIRE(container->CodeChunk() != nullptr);
    REQUIRE(container->CodeChunk()->fourcc == "SHEX");
}

TEST_CASE("MasterReader selects DXBC and exposes the instruction token", "[dxbc]")
{
    const auto bytes = MakeMinimalDxbc();

    SR::Frontend::MasterReader reader;
    const auto shader = reader.Read(bytes);

    REQUIRE(shader.has_value());
    REQUIRE(shader->format == SR::Frontend::BinaryFormat::Dxbc);
    REQUIRE(shader->stage == SR::IL::Stage::Compute);
    REQUIRE(shader->programVersion == 0x00050050u);
    REQUIRE(shader->functions.size() == 1);
    REQUIRE(shader->functions[0].instructions.size() == 1);
    REQUIRE(shader->functions[0].instructions[0].opcode == 62u);
    REQUIRE(shader->functions[0].instructions[0].rawTokens.size() == 1);
    REQUIRE(shader->functions[0].instructions[0].rawTokens[0] == 0x0100003eu);
}

TEST_CASE("MasterReader rejects unknown binaries", "[dxbc]")
{
    const std::vector<std::uint8_t> bytes{'N', 'O', 'P', 'E'};

    SR::Frontend::MasterReader reader;
    const auto shader = reader.Read(bytes);

    REQUIRE_FALSE(shader.has_value());
    REQUIRE(shader.error().code == "SR-FRONTEND-002");
}
