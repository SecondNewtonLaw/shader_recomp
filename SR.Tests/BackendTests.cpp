#include <array>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "SR/Backend/Reemitter.hpp"
#include "SR/Common/IL/Builder.hpp"
#include "SR/Common/IL/Module.hpp"
#include "SR/Common/IL/Verifier.hpp"

using namespace SR;

TEST_CASE("Backend rejects a module without an entry function", "[backend]")
{
    IL::Module module;
    module.stage = IL::Stage::Compute;
    module.entryPoint = "main";

    const auto result = Backend::Reemit(module);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().severity == Severity::Error);
    REQUIRE(result.error().code == "SR-SPV-020");
}

TEST_CASE("Backend emits a geometry stage that emits vertices", "[backend][geometry]")
{
    IL::Module module;
    module.stage = IL::Stage::Geometry;
    module.entryPoint = "main";
    module.architecture = "gfx1030";
    module.executionModes.outputVertices = 3;

    // The emitter interns the scalar types it needs for its own constants.
    (void)module.types.U32();
    (void)module.types.Bool();
    (void)module.types.I32();

    IL::SignatureDecl output;
    output.space = IL::Space::Output;
    output.location = 0;
    output.type = module.types.Vector(module.types.F32(), 4);
    module.outputs.push_back(output);

    IL::Function& function = IL::CreateFunction(module, "main", module.types.Void());
    function.isEntry = true;
    module.entryFunction = function.id;

    IL::FunctionBuilder builder(module, function);
    const IL::BlockId entry = builder.CreateBlock("entry");
    builder.SetEntry(entry);
    builder.SetCurrentBlock(entry);

    IL::StageIoDesc store;
    store.space = IL::Space::Output;
    store.location = 0;
    store.component = 0;
    const IL::PayloadId payload = module.payloads.Add(store);

    for (int vertex = 0; vertex < 3; ++vertex)
    {
        const IL::ValueId x = builder.ConstantF32(static_cast<float>(vertex));
        builder.EmitVoid(IL::Op::Intrinsic,
                         static_cast<std::uint16_t>(IL::IntrinsicId::StageOutput),
                         {x},
                         {},
                         payload,
                         0,
                         0);
        builder.EmitVoid(IL::Op::Intrinsic, static_cast<std::uint16_t>(IL::IntrinsicId::EmitVertex), {});
    }
    builder.EmitVoid(IL::Op::Intrinsic, static_cast<std::uint16_t>(IL::IntrinsicId::EndPrimitive), {});

    IL::Terminator terminator;
    terminator.kind = IL::TerminatorKind::Ret;
    builder.Terminate(builder.CurrentBlock(), terminator);

    const auto verified = IL::VerifyModule(module);
    if (!verified)
        INFO(SR::ToString(verified.error()));
    REQUIRE(verified.has_value());

    const auto emission = Backend::Reemit(module);
    if (!emission)
        WARN(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());

    const auto hasOpcode = [&](std::uint16_t opcode) {
        for (std::size_t i = 5; i < emission->words.size();) {
            const std::uint32_t word = emission->words[i];
            if ((word & 0xFFFFu) == opcode)
                return true;
            i += word >> 16;
        }
        return false;
    };

    // Geometry outputs are the plain per-vertex variables other stages use;
    // OpEmitVertex captures them, so an arrayed output is an interface the
    // driver does not accept (glslang emits plain variables too).
    REQUIRE_FALSE(hasOpcode(28)); // OpTypeArray
    REQUIRE(hasOpcode(218));      // OpEmitVertex
    REQUIRE(hasOpcode(219));      // OpEndPrimitive
}

TEST_CASE("Backend maps every geometry topology to a valid execution mode", "[backend][geometry]")
{
    // Every topology pair must produce a module the validator accepts, and the
    // execution mode must be the SPIR-V enum for that topology: InputPoints is
    // 19 and OutputLineStrip is 28, not the neighbouring modes that happen to
    // read like them.
    const std::array<std::pair<IL::InputTopology, std::uint32_t>, 5> inputs = {
        std::pair{IL::InputTopology::Points, 19u},
        std::pair{IL::InputTopology::Lines, 20u},
        std::pair{IL::InputTopology::LinesAdjacency, 21u},
        std::pair{IL::InputTopology::Triangles, 22u},
        std::pair{IL::InputTopology::TrianglesAdjacency, 23u},
    };
    const std::array<std::pair<IL::OutputTopology, std::uint32_t>, 3> outputs = {
        std::pair{IL::OutputTopology::Points, 27u},
        std::pair{IL::OutputTopology::LineStrip, 28u},
        std::pair{IL::OutputTopology::TriangleStrip, 29u},
    };

    for (const auto& [input, expectedInput] : inputs)
    {
        for (const auto& [output, expectedOutput] : outputs)
        {
            IL::Module module;
            module.stage = IL::Stage::Geometry;
            module.entryPoint = "main";
            module.architecture = "gfx1030";
            module.executionModes.inputTopology = input;
            module.executionModes.outputTopology = output;
            module.executionModes.outputVertices = 2;

            (void)module.types.U32();
            (void)module.types.Bool();
            (void)module.types.I32();

            IL::Function& function = IL::CreateFunction(module, "main", module.types.Void());
            function.isEntry = true;
            module.entryFunction = function.id;

            IL::FunctionBuilder builder(module, function);
            const IL::BlockId entry = builder.CreateBlock("entry");
            builder.SetEntry(entry);
            builder.SetCurrentBlock(entry);
            IL::Terminator terminator;
            terminator.kind = IL::TerminatorKind::Ret;
            builder.Terminate(builder.CurrentBlock(), terminator);

            const auto emission = Backend::Reemit(module);
            if (!emission)
                INFO(SR::ToString(emission.error()));
            REQUIRE(emission.has_value());

            std::array<bool, 64> modes{};
            for (std::size_t index = 5; index < emission->words.size();)
            {
                const std::uint32_t word = emission->words[index];
                const std::uint32_t length = word >> 16;
                if ((word & 0xFFFFu) == 16 && length >= 3) // OpExecutionMode
                {
                    const std::uint32_t mode = emission->words[index + 2];
                    if (mode < modes.size())
                        modes[mode] = true;
                }
                index += length;
            }

            INFO("input " << expectedInput << " output " << expectedOutput);
            REQUIRE(modes[expectedInput]);
            REQUIRE(modes[expectedOutput]);
            REQUIRE(modes[26]); // OutputVertices
        }
    }
}

TEST_CASE("Backend refuses a geometry input it cannot shape", "[backend][geometry]")
{
    // A geometry input is an array indexed by the input vertex; emitting a
    // plain input variable would quietly change the interface, so the gap is a
    // diagnostic until the array form is built.
    IL::Module module;
    module.stage = IL::Stage::Geometry;
    module.entryPoint = "main";
    module.architecture = "gfx1030";

    (void)module.types.U32();
    (void)module.types.Bool();
    (void)module.types.I32();

    IL::SignatureDecl input;
    input.space = IL::Space::Input;
    input.location = 0;
    input.type = module.types.Vector(module.types.F32(), 4);
    module.inputs.push_back(input);

    IL::Function& function = IL::CreateFunction(module, "main", module.types.Void());
    function.isEntry = true;
    module.entryFunction = function.id;

    IL::FunctionBuilder builder(module, function);
    const IL::BlockId entry = builder.CreateBlock("entry");
    builder.SetEntry(entry);
    builder.SetCurrentBlock(entry);
    IL::Terminator terminator;
    terminator.kind = IL::TerminatorKind::Ret;
    builder.Terminate(builder.CurrentBlock(), terminator);

    const auto emission = Backend::Reemit(module);
    REQUIRE_FALSE(emission.has_value());
    REQUIRE(emission.error().code == "SR-SPV-094");
}
