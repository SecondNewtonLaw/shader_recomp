#include <array>
#include <cstdint>
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

} // namespace

TEST_CASE("gfx lineage inherits and overrides opcodes", "[gfx][decode]")
{
    using namespace SR::Frontend::Gfx;

    // SOP1 S_MOV_B32 is opcode 3 on RDNA1/2 and 0 from RDNA3.
    const std::array<std::uint32_t, 1> gfx10Move = {0xBE800300u};
    const std::array<std::uint32_t, 1> gfx12Move = {0xBE800000u};

    const auto legacy = Decode(gfx10Move, Arch::Gfx10, 0);
    REQUIRE(legacy.has_value());
    REQUIRE(legacy->op == Op::S_MovB32);

    const auto current = Decode(gfx12Move, Arch::Gfx12, 0);
    REQUIRE(current.has_value());
    REQUIRE(current->op == Op::S_MovB32);

    // The old opcode is removed, not inherited, once RDNA3 renumbers it.
    const auto stale = Decode(gfx10Move, Arch::Gfx12, 0);
    REQUIRE_FALSE(stale.has_value());
}

TEST_CASE("gfx lineage renumbers SOP2 and SOPP", "[gfx][decode]")
{
    using namespace SR::Frontend::Gfx;

    const std::array<std::uint32_t, 1> gfx10And = {0x87000000u}; // SOP2 opcode 14
    const std::array<std::uint32_t, 1> gfx12And = {0x8B000000u}; // SOP2 opcode 22

    const auto oldAnd = Decode(gfx10And, Arch::Gfx10, 0);
    REQUIRE(oldAnd.has_value());
    REQUIRE(oldAnd->op == Op::S_AndB32);

    const auto newAnd = Decode(gfx12And, Arch::Gfx12, 0);
    REQUIRE(newAnd.has_value());
    REQUIRE(newAnd->op == Op::S_AndB32);

    const std::array<std::uint32_t, 1> gfx10End = {0xBF810000u}; // SOPP opcode 1
    const std::array<std::uint32_t, 1> gfx12End = {0xBFB00000u}; // SOPP opcode 48

    const auto oldEnd = Decode(gfx10End, Arch::Gfx10, 0);
    REQUIRE(oldEnd.has_value());
    REQUIRE(oldEnd->op == Op::S_Endpgm);

    const auto newEnd = Decode(gfx12End, Arch::Gfx12, 0);
    REQUIRE(newEnd.has_value());
    REQUIRE(newEnd->op == Op::S_Endpgm);
}

TEST_CASE("gfx1200 VOP2 operands decode to register classes", "[gfx][decode]")
{
    using namespace SR::Frontend::Gfx;

    const std::array<std::uint32_t, 1> add = {0x06020100u}; // v_add_f32 v1, v0, v0
    const auto instruction = Decode(add, Arch::Gfx12, 0);
    REQUIRE(instruction.has_value());
    REQUIRE(instruction->op == Op::V_AddF32);
    REQUIRE(instruction->operandCount == 3);

    REQUIRE(instruction->operands[0].kind == OperandKind::Vgpr);
    REQUIRE(instruction->operands[0].value == 1);
    REQUIRE(instruction->operands[1].kind == OperandKind::Vgpr);
    REQUIRE(instruction->operands[1].value == 0);
    REQUIRE(instruction->operands[2].kind == OperandKind::Vsrc);
    REQUIRE(instruction->operands[2].value == 0);
}

TEST_CASE("gfx generation fixtures lift and emit across the lineage", "[gfx][lineage][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    struct Case
    {
        const char* directory;
        SR::Frontend::Gfx::Arch arch;
    };

    const Case cases[] = {
        {"gfx10", SR::Frontend::Gfx::Arch::Gfx10},
        {"gfx1013", SR::Frontend::Gfx::Arch::Gfx1013},
        {"gfx1030", SR::Frontend::Gfx::Arch::Gfx103},
        {"gfx1100", SR::Frontend::Gfx::Arch::Gfx11},
        {"gfx1150", SR::Frontend::Gfx::Arch::Gfx115},
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        for (const char* name : {"basic", "loop"})
        {
            INFO(testCase.directory << "/" << name);
            const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory /
                                               (std::string(name) + ".hsaco");
            if (!std::filesystem::exists(path))
                continue;

            const auto bytes = ReadFile(path);
            REQUIRE_FALSE(bytes.empty());

            SR::Frontend::MasterReader reader;
            const auto shader = reader.Read(bytes);
            REQUIRE(shader.has_value());

            auto module = SR::Frontend::Gfx::Lift(*shader, testCase.arch);
            if (!module.has_value())
                INFO(SR::ToString(module.error()));
            REQUIRE(module.has_value());
            REQUIRE(SR::IL::VerifyModule(*module).has_value());

            if (std::string_view(name) == "loop")
            {
                SR::IL::ValueId s0 = SR::IL::InvalidValue;
                for (const auto& slot : module->registerSlots)
                {
                    if (slot.kind == SR::IL::RegisterClass::Sgpr && slot.index == 0)
                        s0 = slot.alloca;
                }
                REQUIRE(s0 != SR::IL::InvalidValue);
                const auto execution = SR::IL::Interpret(*module);
                REQUIRE(execution.has_value());
                REQUIRE(execution->memory.at(s0) == 10);
            }

            REQUIRE(SR::IL::RunPipeline(*module).has_value());
            REQUIRE(SR::IL::VerifyModule(*module).has_value());

            const auto emission = SR::Backend::Reemit(*module);
            if (!emission.has_value())
                INFO(SR::ToString(emission.error()));
            REQUIRE(emission.has_value());
            REQUIRE(emission->words[0] == 0x07230203u);
        }
    }
#endif
}

TEST_CASE("gfx1200 loop lifts, interprets and emits validated SPIR-V", "[gfx][lift][control-flow]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path =
        std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "loop.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("gfx1200 loop fixture is not present");
        return;
    }

    const auto bytes = ReadFile(path);
    REQUIRE_FALSE(bytes.empty());

    SR::Frontend::MasterReader reader;
    const auto shader = reader.Read(bytes);
    REQUIRE(shader.has_value());

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx12);
    if (!module.has_value())
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    // The loop counts s0 from 0 to 10.
    SR::IL::ValueId s0 = SR::IL::InvalidValue;
    for (const auto& slot : module->registerSlots)
    {
        if (slot.kind == SR::IL::RegisterClass::Sgpr && slot.index == 0)
            s0 = slot.alloca;
    }
    REQUIRE(s0 != SR::IL::InvalidValue);

    const auto execution = SR::IL::Interpret(*module);
    if (!execution.has_value())
        INFO(SR::ToString(execution.error()));
    REQUIRE(execution.has_value());
    {
        std::string memory;
        for (const auto& [slot, value] : execution->memory)
            memory += "%" + std::to_string(slot) + "=" + std::to_string(value) + " ";
        INFO("slots: " << memory);
        REQUIRE(execution->memory.at(s0) == 10);
    }

    REQUIRE(SR::IL::AnalyzeCfg(*module).has_value());
    REQUIRE(SR::IL::BuildSsa(*module).has_value());
    REQUIRE(SR::IL::Optimize(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    bool hasPhi = false;
    bool hasAlloca = false;
    for (const auto& block : module->functions[0].blocks)
    {
        for (const auto& instruction : block.body)
        {
            hasPhi = hasPhi || instruction.op == SR::IL::Op::Phi;
            hasAlloca = hasAlloca || instruction.op == SR::IL::Op::Alloca;
        }
    }
    REQUIRE(hasPhi);
    REQUIRE_FALSE(hasAlloca);

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission.has_value())
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
    REQUIRE(emission->words[0] == 0x07230203u);
#endif
}

TEST_CASE("gfx1200 test ABI store lifts and emits", "[gfx][store][test-abi]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "store.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("gfx1200 store fixture is not present");
        return;
    }

    const auto bytes = ReadFile(path);
    REQUIRE_FALSE(bytes.empty());

    SR::Frontend::MasterReader reader;
    const auto shader = reader.Read(bytes);
    REQUIRE(shader.has_value());

    auto module = SR::Frontend::Gfx::Lift(*shader,
                                         SR::Frontend::Gfx::Arch::Gfx12,
                                         "main",
                                         SR::Frontend::Gfx::LiftOptions{true});
    if (!module.has_value())
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());
    REQUIRE(module->testAbi);
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const std::string dump = SR::IL::DumpModule(*module);
    REQUIRE(dump.find("test.buffer-store") != std::string::npos);

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission.has_value())
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
    REQUIRE(emission->words[0] == 0x07230203u);
#endif
}

TEST_CASE("gfx1200 inline constants decode", "[gfx][decode]")
{
    using namespace SR::Frontend::Gfx;

    REQUIRE(InlineInteger(128) == 0);
    REQUIRE(InlineInteger(129) == 1);
    REQUIRE(InlineInteger(192) == 64);
    REQUIRE(InlineInteger(193) == 0xFFFFFFFFu);
    REQUIRE(InlineInteger(208) == 0xFFFFFFF0u);

    const auto one = InlineFloatBits(242);
    REQUIRE(one.has_value());
    REQUIRE(*one == 0x3F800000u);
}

TEST_CASE("gfx1200 fixture lifts and emits validated SPIR-V", "[gfx][lift]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path =
        std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "basic.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("gfx1200 fixture is not present");
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

    auto module = SR::Frontend::Gfx::Lift(*shader, SR::Frontend::Gfx::Arch::Gfx12);
    if (!module.has_value())
        INFO(SR::ToString(module.error()));
    REQUIRE(module.has_value());

    const auto verified = SR::IL::VerifyModule(*module);
    if (!verified.has_value())
        INFO(SR::ToString(verified.error()));
    REQUIRE(verified.has_value());

    const std::string dump = SR::IL::DumpModule(*module);
    REQUIRE(dump.find("binary.fadd") != std::string::npos);

    REQUIRE(SR::IL::AnalyzeCfg(*module).has_value());
    REQUIRE(SR::IL::BuildSsa(*module).has_value());
    REQUIRE(SR::IL::Optimize(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    for (const auto& block : module->functions[0].blocks)
    {
        for (const auto& instruction : block.body)
            REQUIRE(instruction.op != SR::IL::Op::Alloca);
    }

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission.has_value())
        INFO(SR::ToString(emission.error()));
    REQUIRE(emission.has_value());
    REQUIRE(emission->words.size() > 5);
    REQUIRE(emission->words[0] == 0x07230203u);
#endif
}
