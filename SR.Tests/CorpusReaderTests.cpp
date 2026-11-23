#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SR/Frontend/MasterReader.hpp"

namespace {

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("Corpus DXBC and DXIL binaries are readable", "[corpus][reader]")
{
#if !defined(SR_CORPUS_DIR)
    SUCCEED("SR_CORPUS_DIR is not defined");
#else
    const std::filesystem::path corpus{SR_CORPUS_DIR};
    if (!std::filesystem::exists(corpus))
    {
        SUCCEED("corpus directory is not present");
        return;
    }

    SR::Frontend::MasterReader reader;

    std::size_t dxbcCount = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(corpus / "dxbc"))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".dxbc")
            continue;

        const auto bytes = ReadFile(entry.path());
        INFO(entry.path().string());
        REQUIRE_FALSE(bytes.empty());

        const auto shader = reader.Read(bytes);
        if (!shader.has_value())
            INFO(SR::ToString(shader.error()));
        REQUIRE(shader.has_value());
        REQUIRE(shader->format == SR::Frontend::BinaryFormat::Dxbc);
        REQUIRE_FALSE(shader->functions.empty());
        REQUIRE_FALSE(shader->functions[0].instructions.empty());
        ++dxbcCount;
    }

    std::size_t dxilCount = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(corpus / "dxil"))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".dxil")
            continue;

        const auto bytes = ReadFile(entry.path());
        INFO(entry.path().string());
        REQUIRE_FALSE(bytes.empty());

        const auto shader = reader.Read(bytes);
        if (!shader.has_value())
            INFO(SR::ToString(shader.error()));
        REQUIRE(shader.has_value());
        REQUIRE(shader->format == SR::Frontend::BinaryFormat::Dxil);
        REQUIRE_FALSE(shader->rawBytecode.empty());
        REQUIRE(shader->programVersion != 0u);
        ++dxilCount;
    }

    REQUIRE(dxbcCount > 0u);
    REQUIRE(dxilCount > 0u);
#endif
}
