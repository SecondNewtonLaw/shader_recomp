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

TEST_CASE("geometry stage emits its exported vertices", "[gfx][graphics][geometry]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("geometry.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("geometry fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Geometry;

    SR::Frontend::Gfx::LiftOptions options;
    options.geometryOutputVertices = 3;
    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(module->stage == SR::IL::Stage::Geometry);
    REQUIRE(module->executionModes.outputVertices == 3);
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    // The fixture writes a colour and a position per vertex: two outputs, one
    // of them the position builtin.
    REQUIRE(module->outputs.size() == 2);

    const auto execution = SR::IL::Interpret(*module);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());

    // exp pos0 begins a vertex and exp prim closes the primitive, so the three
    // exports are three emitted vertices and the colour is written once.
    REQUIRE(execution->vertices.size() == 3);

    const std::array<std::array<std::uint32_t, 2>, 3> expected = {
        std::array<std::uint32_t, 2>{FloatBits(-1.0f), FloatBits(-1.0f)},
        std::array<std::uint32_t, 2>{FloatBits(3.0f), FloatBits(-1.0f)},
        std::array<std::uint32_t, 2>{FloatBits(-1.0f), FloatBits(3.0f)},
    };

    for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
    {
        INFO("vertex " << vertex);
        const auto& emitted = execution->vertices[vertex];
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, 0)) == expected[vertex][0]);
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, 1)) == expected[vertex][1]);
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 0)) == FloatBits(1.0f));
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 1)) == 0u);
    }

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());

    std::size_t emitted = 0;
    std::size_t primitives = 0;
    std::size_t arrays = 0;
    for (std::size_t index = 5; index < emission->words.size();)
    {
        const std::uint32_t word = emission->words[index];
        const std::uint16_t opcode = static_cast<std::uint16_t>(word & 0xFFFFu);
        if (opcode == 218)
            ++emitted;
        else if (opcode == 219)
            ++primitives;
        else if (opcode == 28)
            ++arrays;
        index += word >> 16;
    }

    // One OpEmitVertex per vertex, one OpEndPrimitive, and no arrayed output:
    // a geometry output is the plain per-vertex variable, captured by emit.
    REQUIRE(emitted == 3);
    REQUIRE(primitives == 1);
    REQUIRE(arrays == 0);
#endif
}

TEST_CASE("geometry stage reads the input vertices", "[gfx][graphics][geometry]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("geometry_read.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("geometry fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Geometry;

    SR::Frontend::Gfx::LiftOptions options;
    options.geometryOutputVertices = 3;
    options.geometryInputTopology = SR::IL::InputTopology::Triangles;
    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    // The input ring names position and location 0; both are declared as
    // geometry inputs, and the position output is built.
    REQUIRE(module->inputs.size() == 2);
    const bool positionInput = std::any_of(module->inputs.begin(), module->inputs.end(), [](const auto& decl) {
        return decl.builtin == SR::IL::Builtin::Position && decl.space == SR::IL::Space::Input;
    });
    REQUIRE(positionInput);

    // The triangle fixture writes (-1,-1), (1,1), (3,-1); the geometry fixture
    // halves x and y and passes the colour through.
    const std::array<std::array<std::uint32_t, 2>, 3> positions = {
        std::array<std::uint32_t, 2>{FloatBits(-1.0f), FloatBits(-1.0f)},
        std::array<std::uint32_t, 2>{FloatBits(1.0f), FloatBits(1.0f)},
        std::array<std::uint32_t, 2>{FloatBits(3.0f), FloatBits(-1.0f)},
    };

    SR::IL::ExecutionOptions executionOptions;
    for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
    {
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::Position, 0, 0, vertex)] = positions[vertex][0];
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::Position, 0, 1, vertex)] = positions[vertex][1];
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::Position, 0, 2, vertex)] = 0u;
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::Position, 0, 3, vertex)] = FloatBits(1.0f);
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::None, 0, 0, vertex)] = FloatBits(1.0f);
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::None, 0, 1, vertex)] = 0u;
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::None, 0, 2, vertex)] = 0u;
        executionOptions.stageInputVertices[SR::IL::StageInputVertexKey(
            SR::IL::Builtin::None, 0, 3, vertex)] = FloatBits(1.0f);
    }

    const auto execution = SR::IL::Interpret(*module, executionOptions);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    REQUIRE(execution->vertices.size() == 3);

    for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
    {
        INFO("vertex " << vertex);
        const auto& emitted = execution->vertices[vertex];
        // The position operand is halved; w is passed through.
        const std::uint32_t expectedX = FloatBits(vertex == 0 ? -0.5f : (vertex == 1 ? 0.5f : 1.5f));
        const std::uint32_t expectedY = FloatBits(vertex == 1 ? 0.5f : -0.5f);
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, 0)) == expectedX);
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, 1)) == expectedY);
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 0)) == FloatBits(1.0f));
    }

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());

    std::size_t arrays = 0;
    std::size_t inputs = 0;
    for (std::size_t index = 5; index < emission->words.size();)
    {
        const std::uint32_t word = emission->words[index];
        const std::uint16_t opcode = static_cast<std::uint16_t>(word & 0xFFFFu);
        if (opcode == 28) // OpTypeArray
            ++arrays;
        index += word >> 16;
    }
    (void)inputs;

    // Two geometry inputs (position and location 0) and the output side, which
    // is not arrayed.
    REQUIRE(arrays == 2);
#endif
}

TEST_CASE("geometry stage publishes staged vertices by message", "[gfx][graphics][geometry]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("geometry_staged.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("geometry fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Geometry;

    SR::Frontend::Gfx::LiftOptions options;
    options.geometryOutputVertices = 3;
    options.geometryOutputRegisters = 2;
    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    if (!module)
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(module->executionModes.outputRegisters == 2);
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    // Two four-component registers per vertex: position and location 0.
    REQUIRE(module->outputs.size() == 2);

    const auto execution = SR::IL::Interpret(*module);
    if (!execution)
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());

    // The fixture stages a full-screen triangle and emits three vertices; the
    // last message also ends the strip.
    REQUIRE(execution->vertices.size() == 3);
    const std::array<std::array<std::uint32_t, 2>, 3> positions = {
        std::array<std::uint32_t, 2>{FloatBits(-1.0f), FloatBits(-1.0f)},
        std::array<std::uint32_t, 2>{FloatBits(3.0f), FloatBits(-1.0f)},
        std::array<std::uint32_t, 2>{FloatBits(-1.0f), FloatBits(3.0f)},
    };

    for (std::uint32_t vertex = 0; vertex < 3; ++vertex)
    {
        INFO("vertex " << vertex);
        const auto& emitted = execution->vertices[vertex];
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, 0)) == positions[vertex][0]);
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, 1)) == positions[vertex][1]);
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::Position, 0, 3)) == FloatBits(1.0f));
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 0)) == FloatBits(1.0f));
        REQUIRE(emitted.at(SR::IL::StageIoKey(SR::IL::Builtin::None, 0, 1)) == 0u);
    }

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());

    std::size_t emitted = 0;
    std::size_t primitives = 0;
    std::size_t arrays = 0;
    for (std::size_t index = 5; index < emission->words.size();)
    {
        const std::uint32_t word = emission->words[index];
        const std::uint16_t opcode = static_cast<std::uint16_t>(word & 0xFFFFu);
        if (opcode == 218)
            ++emitted;
        else if (opcode == 219)
            ++primitives;
        else if (opcode == 28)
            ++arrays;
        index += word >> 16;
    }
    REQUIRE(emitted == 3);
    REQUIRE(primitives == 1);
    // The staging is a Function array; the outputs themselves stay plain.
    REQUIRE(arrays == 1);
#endif
}

TEST_CASE("image queries, gather and atomics lift and execute", "[gfx][graphics][image]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto lift = [](const char* name, bool push) -> std::optional<SR::IL::Module> {
        const auto bytes = ReadFile(Fixture(name));
        if (bytes.empty())
            return std::nullopt;
        auto shader = SR::Frontend::MasterReader{}.Read(bytes);
        if (!shader)
            return std::nullopt;
        shader->stage = SR::IL::Stage::Compute;

        SR::Frontend::Gfx::LiftOptions options;
        options.testAbi = true;
        // The atomic fixture needs the surface format, because the descriptor
        // format is not decoded; the others do not use it.
        options.imageFormat = 33; // R32ui
        if (push)
        {
            options.userData.assign(12, 0u);
            options.userDataFromPush = true;
        }
        auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
        if (!module)
        {
            INFO(SR::ToString(module.error()));
            return std::nullopt;
        }
        return std::optional<SR::IL::Module>(std::move(*module));
    };

    SECTION("resinfo reports the surface size")
    {
        auto module = lift("imageresinfo.hsaco", true);
        if (!module)
        {
            SUCCEED("image fixture is not present");
            return;
        }
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        SR::IL::ExecutionOptions options;
        options.imageExtents[1] = {4, 4};
        const auto execution = SR::IL::Interpret(*module, options);
        if (!execution)
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 4u);
        REQUIRE(execution->buffer.at(1) == 4u);
        REQUIRE(execution->buffer.at(2) == 1u);

        const auto emission = SR::Backend::Reemit(*module);
        std::string emissionError;
        if (!emission)
            emissionError = SR::ToString(emission.error());
        INFO(emissionError);
        REQUIRE(emission.has_value());
    }

    SECTION("gather4 reads the neighbouring texels")
    {
        auto module = lift("imagegather4.hsaco", true);
        if (!module)
        {
            SUCCEED("image fixture is not present");
            return;
        }
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        SR::IL::ExecutionOptions options;
        options.imageExtents[8] = {4, 4};
        for (std::uint32_t y = 0; y < 4; ++y)
        {
            for (std::uint32_t x = 0; x < 4; ++x)
            {
                const std::uint64_t key = (8ull << 40) | (static_cast<std::uint64_t>(y) << 20) | x;
                const std::uint32_t base = 1 + x + 4 * y;
                options.images[key] = {base, base + 1, base + 2, base + 3};
            }
        }

        const auto execution = SR::IL::Interpret(*module, options);
        if (!execution)
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());
        // 0.125 of 4 texels is the 2x2 footprint at the origin, in the data
        // register order the hardware delivers (measured on the device).
        REQUIRE(execution->buffer.at(0) == 5u);
        REQUIRE(execution->buffer.at(1) == 6u);
        REQUIRE(execution->buffer.at(2) == 2u);
        REQUIRE(execution->buffer.at(3) == 1u);

        const auto emission = SR::Backend::Reemit(*module);
        std::string emissionError;
        if (!emission)
            emissionError = SR::ToString(emission.error());
        INFO(emissionError);
        REQUIRE(emission.has_value());
    }

    SECTION("an explicit level sample reads the level it names")
    {
        auto module = lift("imagemip.hsaco", true);
        if (!module)
        {
            SUCCEED("image fixture is not present");
            return;
        }
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        SR::IL::ExecutionOptions options;
        options.imageExtents[8] = {4, 4};
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            const std::uint64_t key = (8ull << 40) | x;
            const std::uint32_t base = 1 + x;
            options.images[key] = {base, base + 1, base + 2, base + 3};
        }

        const auto execution = SR::IL::Interpret(*module, options);
        std::string executionError;
        if (!execution)
            executionError = SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());
        // Level zero of the bound surface is texel (0,0). The test ABI's image
        // contents are raw dwords, so the texel is 1, 2, 3, 4 here; the device
        // holds the same dwords as floats.
        REQUIRE(execution->buffer.at(0) == 1u);
        REQUIRE(execution->buffer.at(1) == 2u);
        REQUIRE(execution->buffer.at(2) == 3u);
        REQUIRE(execution->buffer.at(3) == 4u);

        const auto emission = SR::Backend::Reemit(*module);
        std::string emissionError;
        if (!emission)
            emissionError = SR::ToString(emission.error());
        INFO(emissionError);
        REQUIRE(emission.has_value());
    }

    SECTION("a gradient sample reads the zero-gradient texel")
    {
        auto module = lift("imagesampled.hsaco", true);
        if (!module)
        {
            SUCCEED("image fixture is not present");
            return;
        }
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        SR::IL::ExecutionOptions options;
        options.imageExtents[8] = {4, 4};
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            const std::uint64_t key = (8ull << 40) | x;
            options.images[key] = {1 + x, 2 + x, 3 + x, 4 + x};
        }

        const auto execution = SR::IL::Interpret(*module, options);
        std::string executionError;
        if (!execution)
            executionError = SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 1u);
        REQUIRE(execution->buffer.at(1) == 2u);
        REQUIRE(execution->buffer.at(2) == 3u);
        REQUIRE(execution->buffer.at(3) == 4u);

        const auto emission = SR::Backend::Reemit(*module);
        std::string emissionError;
        if (!emission)
            emissionError = SR::ToString(emission.error());
        INFO(emissionError);
        REQUIRE(emission.has_value());
    }

    SECTION("a mip load on a storage image is refused with its reason")
    {
        const auto bytes = ReadFile(Fixture("imageloadmip.hsaco"));
        if (bytes.empty())
        {
            SUCCEED("image fixture is not present");
            return;
        }
        auto shader = SR::Frontend::MasterReader{}.Read(bytes);
        REQUIRE(shader.has_value());
        shader->stage = SR::IL::Stage::Compute;

        SR::Frontend::Gfx::LiftOptions options;
        options.testAbi = true;
        options.userData.assign(4, 0u);
        options.userDataFromPush = true;
        auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
        REQUIRE_FALSE(module.has_value());
        REQUIRE(module.error().code == "SR-GFX-LIFT-069");
    }

    SECTION("an atomic without a declared format is refused")
    {
        const auto bytes = ReadFile(Fixture("imageatomicadd.hsaco"));
        if (bytes.empty())
        {
            SUCCEED("image fixture is not present");
            return;
        }
        auto shader = SR::Frontend::MasterReader{}.Read(bytes);
        REQUIRE(shader.has_value());
        shader->stage = SR::IL::Stage::Compute;

        SR::Frontend::Gfx::LiftOptions options;
        options.testAbi = true;
        options.userData.assign(4, 0u);
        options.userDataFromPush = true;
        auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
        REQUIRE_FALSE(module.has_value());
        REQUIRE(module.error().code == "SR-GFX-LIFT-068");
    }

    SECTION("an atomic add returns the previous value")
    {
        auto module = lift("imageatomicadd.hsaco", true);
        if (!module)
        {
            SUCCEED("image fixture is not present");
            return;
        }
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        SR::IL::ExecutionOptions options;
        const std::uint64_t key = (1ull << 40);
        options.images[key] = {41, 0, 0, 0};

        const auto execution = SR::IL::Interpret(*module, options);
        if (!execution)
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 41u);
        REQUIRE(execution->images.at(key)[0] == 42u);

        const auto emission = SR::Backend::Reemit(*module);
        std::string emissionError;
        if (!emission)
            emissionError = SR::ToString(emission.error());
        INFO(emissionError);
        REQUIRE(emission.has_value());
    }
#endif
}

TEST_CASE("buffer loads move runs of dwords and narrow elements", "[gfx][buffers]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("bufferwide.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("buffer fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    options.userData.assign(4, 0u);
    options.userDataFromPush = true;
    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    std::string liftError;
    if (!module)
        liftError = SR::ToString(module.error());
    INFO(liftError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    std::string executionError;
    if (!execution)
        executionError = SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    // The fixture seeds dword 0 with 0x0000FF81 and dwords 1..3 with a pattern,
    // then copies the wide load, the narrow loads and the dwordx2 round trip
    // into dwords 4..13.
    REQUIRE(execution->buffer.at(4) == 0x0000FF81u);
    REQUIRE(execution->buffer.at(5) == 0x11111111u);
    REQUIRE(execution->buffer.at(6) == 0x22222222u);
    REQUIRE(execution->buffer.at(7) == 0x33333333u);
    REQUIRE(execution->buffer.at(8) == 0x81u);        // ubyte
    REQUIRE(execution->buffer.at(9) == 0xFFFFFF81u);  // sbyte
    REQUIRE(execution->buffer.at(10) == 0x0000FF81u); // ushort
    REQUIRE(execution->buffer.at(11) == 0xFFFFFF81u); // sshort
    REQUIRE(execution->buffer.at(12) == 0x0000FF81u); // dwordx2 round trip
    REQUIRE(execution->buffer.at(13) == 0x11111111u);

    const auto emission = SR::Backend::Reemit(*module);
    std::string emissionError;
    if (!emission)
        emissionError = SR::ToString(emission.error());
    INFO(emissionError);
    REQUIRE(emission.has_value());
#endif
}

TEST_CASE("gfx10 decodes the scalar shift-add and pack forms", "[gfx][scalar]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("scalarnew.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("scalar fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    std::string liftError;
    if (!module)
        liftError = SR::ToString(module.error());
    INFO(liftError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    std::string executionError;
    if (!execution)
        executionError = SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    // s_lshl4_add_u32 dst, src0, src1 is (src0 << 4) + src1 = (1 << 4) + 3,
    // and s_pack_ll_b32_b16 keeps the low half of each source.
    REQUIRE(execution->buffer.at(0) == 19u);
    REQUIRE(execution->buffer.at(1) == 0x00030001u);

    const auto emission = SR::Backend::Reemit(*module);
    std::string emissionError;
    if (!emission)
        emissionError = SR::ToString(emission.error());
    INFO(emissionError);
    REQUIRE(emission.has_value());
#endif
}

TEST_CASE("LDS counters return the value and step it", "[gfx][lds]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("dscounter.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("counter fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    std::string liftError;
    if (!module)
        liftError = SR::ToString(module.error());
    INFO(liftError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    std::string executionError;
    if (!execution)
        executionError = SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    // append returns 0 then 1, consume returns 2, and the second counter
    // starts at 0.
    REQUIRE(execution->buffer.at(0) == 0u);
    REQUIRE(execution->buffer.at(1) == 1u);
    REQUIRE(execution->buffer.at(2) == 2u);
    REQUIRE(execution->buffer.at(3) == 0u);

    const auto emission = SR::Backend::Reemit(*module);
    std::string emissionError;
    if (!emission)
        emissionError = SR::ToString(emission.error());
    INFO(emissionError);
    REQUIRE(emission.has_value());
#endif
}

TEST_CASE("LDS compare-and-store writes only on a match", "[gfx][lds]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const auto bytes = ReadFile(Fixture("dscmpst.hsaco"));
    if (bytes.empty())
    {
        SUCCEED("compare-store fixture is not present");
        return;
    }

    auto shader = SR::Frontend::MasterReader{}.Read(bytes);
    REQUIRE(shader.has_value());
    shader->stage = SR::IL::Stage::Compute;

    SR::Frontend::Gfx::LiftOptions options;
    options.testAbi = true;
    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx103, "main", options);
    std::string liftError;
    if (!module)
        liftError = SR::ToString(module.error());
    INFO(liftError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    std::string executionError;
    if (!execution)
        executionError = SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    // The first compare matched and stored 9, the stale compare left it, and
    // the returning form handed back 9 while storing 11.
    REQUIRE(execution->buffer.at(0) == 9u);
    REQUIRE(execution->buffer.at(1) == 11u);

    const auto emission = SR::Backend::Reemit(*module);
    std::string emissionError;
    if (!emission)
        emissionError = SR::ToString(emission.error());
    INFO(emissionError);
    REQUIRE(emission.has_value());
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
