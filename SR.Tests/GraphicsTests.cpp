#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SR/Backend/Reemitter.hpp"
#include "SR/Common/IL/Dump.hpp"
#include "SR/Common/IL/Interpreter.hpp"
#include "SR/Common/IL/Passes.hpp"
#include "SR/Common/IL/Verifier.hpp"
#include "SR/Frontend/Arch/Gfx/GfxLifter.hpp"
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
    return std::filesystem::path(SR_CORPUS_ROOT) / "gfx1030" / name;
}

std::uint32_t FloatBits(float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace

TEST_CASE("vertex stage lifts exports into stage outputs", "[gfx][graphics][vertex]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("triangle.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("triangle fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Vertex;

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main");
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(module->stage == SR::IL::Stage::Vertex);
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    // One position output and one four-component parameter output.
    REQUIRE(module->outputs.size() == 2);
    const bool hasPosition = std::any_of(module->outputs.begin(), module->outputs.end(), [](const auto& output) {
        return output.builtin == SR::IL::Builtin::Position && output.space == SR::IL::Space::Output;
    });
    REQUIRE(hasPosition);

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
    REQUIRE(emission->words[0] == 0x07230203u);

    // The triangle fixture derives its positions from the vertex index:
    // (2i-1, 2(i&1)-1, 0, 1), so the three vertices cover the centre.
    const std::array<std::array<std::uint32_t, 4>, 3> expected = {
        std::array<std::uint32_t, 4>{FloatBits(-1.0f), FloatBits(-1.0f), 0u, FloatBits(1.0f)},
        std::array<std::uint32_t, 4>{FloatBits(1.0f), FloatBits(1.0f), 0u, FloatBits(1.0f)},
        std::array<std::uint32_t, 4>{FloatBits(3.0f), FloatBits(-1.0f), 0u, FloatBits(1.0f)},
    };

    for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
    {
        SR::IL::ExecutionOptions options;
        options.stageInputs[SR::IL::StageIoKey(SR::IL::Builtin::VertexIndex, 0, 0)] = vertex;
        options.stageInputs[SR::IL::StageIoKey(SR::IL::Builtin::InstanceIndex, 0, 0)] = 0;

        const auto execution = SR::IL::Interpret(*module, options);
        if (!execution)
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());

        for (std::uint32_t component = 0; component < 4; ++component)
        {
            INFO("vertex " << vertex << " component " << component);
            REQUIRE(execution->outputs.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, component))
                    == expected[vertex][component]);
        }
    }
#endif
}

TEST_CASE("fragment stage interpolates and exports colour", "[gfx][graphics][fragment]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("solid.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("solid fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Fragment;

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main");
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(module->stage == SR::IL::Stage::Fragment);
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    REQUIRE(module->inputs.size() == 1);
    REQUIRE(module->inputs.front().location == 0);
    REQUIRE(module->inputs.front().interpolation == SR::IL::Interpolation::Perspective);

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());

    SR::IL::ExecutionOptions options;
    options.stageInputs[SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 0)] = FloatBits(1.0f);

    const auto execution = SR::IL::Interpret(*module, options);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());

    REQUIRE(execution->outputs.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 0)) == FloatBits(1.0f));
    REQUIRE(execution->outputs.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 1)) == 0u);
    REQUIRE(execution->outputs.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 2)) == 0u);
    REQUIRE(execution->outputs.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 3)) == FloatBits(1.0f));
#endif
}

TEST_CASE("interpolation outside a fragment shader is refused", "[gfx][graphics][stage]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("solid.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("solid fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Vertex;

    const auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main");
    REQUIRE_FALSE(module.has_value());
    REQUIRE(module.error().code == "SR-GFX-LIFT-040");
#endif
}

TEST_CASE("a compare mask is a wave ballot", "[gfx][wave][ballot]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("waveballot.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("waveballot fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.wave64 = true;

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const std::string dump = SR::IL::DumpModule(*module);
    REQUIRE(dump.find("wave.ballot") != std::string::npos);

    // The interpreter runs a one-lane wave: the ballot is bit 0, so the
    // popcount is one. On hardware the same module reports the wave width.
    SR::IL::ExecutionOptions executionOptions;
    executionOptions.threadId = {4, 0, 0}; // byte address 4 -> dword 1
    const auto execution = SR::IL::Interpret(*module, executionOptions);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(1) == 1);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("user data seeds the scalar registers", "[gfx][userdata]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("userdata.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("userdata fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData = {41};

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(0) == 41u);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("v_readfirstlane broadcasts the first lane", "[gfx][wave][broadcast]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("readfirst.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("readfirst fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.wave64 = true;

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());

    // The IL uses the wave intrinsic, not a register copy.
    REQUIRE(SR::IL::DumpModule(*module).find("wave.broadcast-first") != std::string::npos);

    // One invocation is a one-lane wave: the value is its own thread id. On the
    // device every lane of a 64-wide wave stores lane 0's id instead.
    SR::IL::ExecutionOptions executionOptions;
    executionOptions.threadId = {5, 0, 0};
    const auto execution = SR::IL::Interpret(*module, executionOptions);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(1) == 5u);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("user data can come from host push constants", "[gfx][userdata][push]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("userdata.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("userdata fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData = {0u}; // the count; the value arrives from the host
    options.userDataFromPush = true;

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::DumpModule(*module).find("user-data[0]") != std::string::npos);

    SR::IL::ExecutionOptions executionOptions;
    executionOptions.userData = {41u};
    const auto execution = SR::IL::Interpret(*module, executionOptions);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(0) == 41u);

    // The module reads a push constant block, which the emitter must declare
    // and list in the entry point interface for SPIR-V 1.4+.
    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
#endif
}
