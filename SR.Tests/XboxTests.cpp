#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SR/Backend/Reemitter.hpp"
#include "SR/Common/IL/Dump.hpp"
#include "SR/Common/IL/Interpreter.hpp"
#include "SR/Common/IL/Passes.hpp"
#include "SR/Common/IL/Verifier.hpp"
#include "SR/Frontend/Arch/Gfx/GfxLifter.hpp"
#include "SR/Frontend/Container/XboxShaderContainer.hpp"
#include "SR/Frontend/MasterReader.hpp"

namespace {

std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::filesystem::path Fixture(const char* name)
{
    return std::filesystem::path(SR_CORPUS_ROOT) / "xbox" / name;
}

} // namespace

TEST_CASE("xbox container resolves a console program", "[xbox][container]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("compute.bin"));
    if (bytes.empty())
    {
        SUCCEED("xbox fixture is not present");
        return;
    }

    const auto container = SR::Frontend::XboxShaderContainer::Parse(bytes);
    if (!container)
        INFO(SR::ToString(container.error()));
    REQUIRE(container.has_value());

    REQUIRE(container->stage() == SR::IL::Stage::Compute);
    REQUIRE(container->fssmVersion() == 0x101u);
    REQUIRE(container->shaderModelMajor() == 6);
    REQUIRE(container->shaderModelMinor() == 0);
    REQUIRE(container->hash().size() == 16);

    // The fixture carries one compute record; the record table resolved it, so
    // no fallback or ambiguity should be reported.
    REQUIRE(container->programs().size() == 1);

    const auto& program = container->programs().front();
    REQUIRE(program.stage == SR::Frontend::XboxStage::Cs);
    REQUIRE(program.wave32);
    REQUIRE(program.hasComputeState);
    REQUIRE(program.threadGroupSize == std::array<std::uint32_t, 3>{64, 1, 1});
    REQUIRE(program.computeRsrc2 == 2);
    REQUIRE(program.vgprCount == 2);
    REQUIRE(program.sgprCount == 2);
    REQUIRE(program.ldsBytes == 0);
    REQUIRE(program.codeSize == 44);

    const auto code = container->code(program);
    REQUIRE(code.size() == 44);
    REQUIRE(code.size() % 4 == 0);
    REQUIRE(container->notes().empty());
#endif
}

TEST_CASE("xbox container rejects a container without XDAT", "[xbox][container]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    auto bytes = ReadFile(Fixture("compute.bin"));
    if (bytes.empty())
    {
        SUCCEED("xbox fixture is not present");
        return;
    }

    // Remove the index: the container still parses as DXBC, but nothing says
    // where the native program is, so the reader must refuse it.
    static constexpr std::uint8_t kXdat[] = {0x58, 0x44, 0x41, 0x54};
    const auto found = std::search(bytes.begin(), bytes.end(), std::begin(kXdat), std::end(kXdat));
    REQUIRE(found != bytes.end());
    std::fill(found, found + 4, 0);

    REQUIRE_FALSE(SR::Frontend::XboxShaderContainer::Probe(bytes));

    const auto container = SR::Frontend::XboxShaderContainer::Parse(bytes);
    REQUIRE_FALSE(container.has_value());
    REQUIRE(container.error().code == "SR-XBOX-002");
#endif
}

TEST_CASE("xbox container and hsaco deliver the same shader", "[xbox][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto consoleBytes = ReadFile(Fixture("compute.bin"));
    const auto hsacoBytes = ReadFile(Fixture("compute.hsaco"));
    if (consoleBytes.empty() || hsacoBytes.empty())
    {
        SUCCEED("xbox fixtures are not present");
        return;
    }

    SR::Frontend::MasterReader reader;

    const auto console = reader.Read(consoleBytes);
    if (!console)
        INFO(SR::ToString(console.error()));
    REQUIRE(console.has_value());
    REQUIRE(console->format == SR::Frontend::BinaryFormat::XboxShader);
    REQUIRE(console->stage == SR::IL::Stage::Compute);

    const auto raw = reader.Read(hsacoBytes);
    REQUIRE(raw.has_value());
    REQUIRE(raw->format == SR::Frontend::BinaryFormat::Hsaco);

    // The container's job ends here: the stage's code must reach the lifter as
    // the same bytes the raw code object carries.
    REQUIRE(console->rawBytecode == raw->rawBytecode);

    const SR::Frontend::Gfx::LiftOptions options{true};

    auto consoleModule = SR::Frontend::Gfx::Lift(*console, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!consoleModule)
        INFO(SR::ToString(consoleModule.error()));
    REQUIRE(consoleModule.has_value());

    auto rawModule = SR::Frontend::Gfx::Lift(*raw, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    REQUIRE(rawModule.has_value());

    REQUIRE(SR::IL::DumpModule(*consoleModule) == SR::IL::DumpModule(*rawModule));
    REQUIRE(SR::IL::VerifyModule(*consoleModule).has_value());
    REQUIRE(SR::IL::RunPipeline(*consoleModule).has_value());
    REQUIRE(SR::IL::RunPipeline(*rawModule).has_value());
    REQUIRE(SR::IL::VerifyModule(*consoleModule).has_value());

    const auto execution = SR::IL::Interpret(*consoleModule);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());

    const auto consoleEmission = SR::Backend::Reemit(*consoleModule);
    if (!consoleEmission)
        INFO(SR::ToString(consoleEmission.error()));
    REQUIRE(consoleEmission.has_value());
    REQUIRE(consoleEmission->words[0] == 0x07230203u);

    const auto rawEmission = SR::Backend::Reemit(*rawModule);
    REQUIRE(rawEmission.has_value());
    REQUIRE(consoleEmission->words == rawEmission->words);
#endif
}

TEST_CASE("stages without an execution model are refused", "[xbox][stage]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("compute.bin"));
    if (bytes.empty())
    {
        SUCCEED("xbox fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());

    shader->stage = SR::IL::Stage::Geometry;

    const auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main");
    REQUIRE_FALSE(module.has_value());
    REQUIRE(module.error().code == "SR-GFX-LIFT-032");
#endif
}
