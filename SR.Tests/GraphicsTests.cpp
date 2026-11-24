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
#include "SR/Frontend/Arch/Gfx/GfxIsa.hpp"
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

TEST_CASE("a buffer descriptor becomes a device address load", "[gfx][descriptor][bda]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("descriptor.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("descriptor fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    // A V# for a buffer at 0x2000: low address, high address, records, flags.
    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData = {0x2000u, 0u, 256u, 0u};

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const std::string dump = SR::IL::DumpModule(*module);
    REQUIRE(dump.find("device.load") != std::string::npos);

    SR::IL::ExecutionOptions executionOptions;
    executionOptions.deviceMemory[0x2000] = 0x1234u;
    const auto execution = SR::IL::Interpret(*module, executionOptions);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());

    // The loaded dword is copied to the test buffer at dword 8.
    REQUIRE(execution->buffer.at(2) == 0x1234u);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
#endif
}

TEST_CASE("MIMG decodes the descriptor registers and the data mask", "[gfx][image][decode]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    // image_sample v[0:3], v[4:5], s[8:15], s[16:19] dmask:0xf dim:2d
    const std::uint32_t words[] = {0xF0800F08u, 0x00820004u};
    const auto decoded = SR::Frontend::Gfx::Decode(words, SR::Frontend::Gfx::Arch::Gfx103, 0);
    if (!decoded)
        INFO(SR::ToString(decoded.error()));
    REQUIRE(decoded.has_value());

    REQUIRE(decoded->op == SR::Frontend::Gfx::Op::ImageSample);
    REQUIRE(decoded->encoding == SR::Frontend::Gfx::Encoding::Mimg);
    REQUIRE(decoded->imageSrsrc == 8);
    REQUIRE(decoded->imageSsamp == 16);
    REQUIRE(decoded->imageDmask == 0xF);
    REQUIRE(decoded->imageDim == 1); // 2D
    REQUIRE(decoded->operandCount == 2);
    REQUIRE(decoded->operands[0].kind == SR::Frontend::Gfx::OperandKind::Vgpr);
    REQUIRE(decoded->operands[0].value == 0);  // vdata v0
    REQUIRE(decoded->operands[1].value == 4);  // vaddr v4
#endif
}

TEST_CASE("image_sample, load and store lift from one descriptor pair", "[gfx][image][stage]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("image.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("image fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    // Without user data the descriptor is missing and the diagnostic names it.
    const auto refused = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main");
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().code == "SR-GFX-LIFT-091");
    REQUIRE(refused.error().message.find("s8") != std::string::npos);

    // With the T# and S# in user data the whole fixture translates.
    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData.assign(12, 0u);

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const std::string dump = SR::IL::DumpModule(*module);
    REQUIRE(dump.find("image.sample") != std::string::npos);
    REQUIRE(dump.find("image.load") != std::string::npos);
    REQUIRE(dump.find("image.store") != std::string::npos);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
#endif
}

TEST_CASE("image_sample reads through a sampler binding", "[gfx][image][sample]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("imagesample.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("imagesample fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData.assign(12, 0u); // T# at s0..s7, S# at s8..s11

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const std::string dump = SR::IL::DumpModule(*module);
    REQUIRE(dump.find("image.sample") != std::string::npos);
    REQUIRE(dump.find("binding=8") != std::string::npos);
    REQUIRE(dump.find("sampler=16") != std::string::npos);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
#endif
}

TEST_CASE("image_store round-trips a texel", "[gfx][image][store]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("imagestore.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("imagestore fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData.assign(8, 0u);

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const std::string dump = SR::IL::DumpModule(*module);
    REQUIRE(dump.find("image.store") != std::string::npos);
    REQUIRE(dump.find("image.load") != std::string::npos);

    // The fixture writes (10,20,30,40) to texel (1,1) and reads it back, so the
    // interpreter's round trip must return exactly those bits with no seeded
    // image contents at all.
    const auto execution = SR::IL::Interpret(*module);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(0) == FloatBits(10.0f));
    REQUIRE(execution->buffer.at(1) == FloatBits(20.0f));
    REQUIRE(execution->buffer.at(2) == FloatBits(30.0f));
    REQUIRE(execution->buffer.at(3) == FloatBits(40.0f));

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
#endif
}

TEST_CASE("a typed buffer access lifts for a 32-bit format", "[gfx][mtbuf][descriptor]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    // Take the descriptor fixture's code object and replace its MUBUF load with
    // a hand-encoded TBUFFER load: opcode 0, data format 4 (32-bit), number
    // format 7 (float), vaddr v0, vdata v1, srsrc s0.
    auto bytes = ReadFile(Fixture("descriptor.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("descriptor fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;
    REQUIRE(shader->rawBytecode.size() >= 12);

    // TBUFFER_LOAD_FORMAT_X: ENC_MTBUF with opcode 0, data format 4 (32-bit),
    // number format 7 (float); the fields sit at bits 19 and 23 of the first
    // dword, so the opcode only owns bits 16..18.
    const std::array<std::uint32_t, 2> typed = {
        0xE8000000u | (4u << 19) | (7u << 23), // opcode, data format, number format
        0x80000000u | (1u << 8)               // soffset 0, vdata v1, vaddr v0
    };
    std::memcpy(shader->rawBytecode.data() + 4, typed.data(), sizeof(typed));

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData = {0x2000u, 0u, 256u, 0u};

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::DumpModule(*module).find("device.load") != std::string::npos);

    SR::IL::ExecutionOptions executionOptions;
    executionOptions.deviceMemory[0x2000] = 0xDEADBEEFu;
    const auto execution = SR::IL::Interpret(*module, executionOptions);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(2) == 0xDEADBEEFu);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());

    // A converting format (8-bit UNORM) is refused with the two field values.
    std::array<std::uint32_t, 2> unsupported = typed;
    unsupported[0] = 0xE8000000u | (1u << 19); // data format 1, number format 0
    std::memcpy(shader->rawBytecode.data() + 4, unsupported.data(), sizeof(unsupported));

    const auto refused = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    REQUIRE_FALSE(refused.has_value());
    REQUIRE(refused.error().code == "SR-GFX-LIFT-094");
#endif
}
