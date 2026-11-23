#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
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

TEST_CASE("SPIR-V modules are readable", "[spirv][reader]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "spirv" / "simple.spv";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("SPIR-V fixture is not present");
        return;
    }

    const auto bytes = ReadFile(path);
    REQUIRE_FALSE(bytes.empty());

    SR::Frontend::MasterReader reader;
    const auto shader = reader.Read(bytes);
    if (!shader.has_value())
        INFO(SR::ToString(shader.error()));
    REQUIRE(shader.has_value());
    REQUIRE(shader->format == SR::Frontend::BinaryFormat::Spirv);
    REQUIRE(shader->architecture == "spirv");
    REQUIRE(shader->stage == SR::IL::Stage::Compute);
    REQUIRE(shader->entryPoint == "main");
    REQUIRE_FALSE(shader->functions.empty());
    REQUIRE_FALSE(shader->functions[0].instructions.empty());
#endif
}

TEST_CASE("Synthetic NVIDIA cubin containers are readable", "[cubin][reader]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "vendor" / "minimal.cubin";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("cubin fixture is not present");
        return;
    }

    const auto bytes = ReadFile(path);
    REQUIRE_FALSE(bytes.empty());

    SR::Frontend::MasterReader reader;
    const auto shader = reader.Read(bytes);
    if (!shader.has_value())
        INFO(SR::ToString(shader.error()));
    REQUIRE(shader.has_value());
    REQUIRE(shader->format == SR::Frontend::BinaryFormat::Cubin);
    REQUIRE(shader->architecture == "sass");
    REQUIRE_FALSE(shader->functions.empty());
    REQUIRE_FALSE(shader->functions[0].instructions.empty());
    REQUIRE_FALSE(shader->rawBytecode.empty());
#endif
}

TEST_CASE("Synthetic AMDGPU code objects are readable", "[hsaco][reader]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "vendor" / "minimal.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("hsaco fixture is not present");
        return;
    }

    const auto bytes = ReadFile(path);
    REQUIRE_FALSE(bytes.empty());

    SR::Frontend::MasterReader reader;
    const auto shader = reader.Read(bytes);
    if (!shader.has_value())
        INFO(SR::ToString(shader.error()));
    REQUIRE(shader.has_value());
    REQUIRE(shader->format == SR::Frontend::BinaryFormat::Hsaco);
    REQUIRE(shader->architecture == "gcn");
    REQUIRE_FALSE(shader->functions.empty());
    REQUIRE_FALSE(shader->functions[0].instructions.empty());
    REQUIRE_FALSE(shader->rawBytecode.empty());
#endif
}
