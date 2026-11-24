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
