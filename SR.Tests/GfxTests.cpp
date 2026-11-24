#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SR/Backend/Reemitter.hpp"
#include "SR/Common/IL/Dump.hpp"
#include "SR/Common/IL/Interpreter.hpp"
#include "SR/Common/IL/Passes.hpp"
#include "SR/Common/IL/Verifier.hpp"
#include "SR/Frontend/Arch/Gfx/GfxIsa.hpp"
#include "SR/Frontend/Arch/Gfx/GfxInterpreter.hpp"
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

std::uint64_t RegisterValue(const SR::IL::Module& module,
                            const SR::IL::ExecutionResult& execution,
                            SR::IL::RegisterClass kind,
                            std::uint32_t index)
{
    for (const auto& entry : module.registerSlots)
    {
        if (entry.kind == kind && entry.index == index)
            return execution.memory.at(entry.alloca);
    }
    return SR::IL::InvalidValue;
}

bool HasOpcode(const std::vector<std::uint32_t>& words, std::uint16_t opcode)
{
    std::size_t index = 5; // skip the SPIR-V header
    while (index < words.size())
    {
        const std::uint32_t word = words[index];
        const std::uint32_t length = word >> 16;
        if (length == 0 || index + length > words.size())
            return false;
        if ((word & 0xFFFFu) == opcode)
            return true;
        index += length;
    }
    return false;
}

constexpr std::uint16_t kSpirvOpPhi = 245;
constexpr std::uint16_t kSpirvOpLoopMerge = 246;
constexpr std::uint16_t kSpirvOpSelectionMerge = 247;
constexpr std::uint16_t kSpirvOpSwitch = 251;

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

    // Renumbering does not leave the old opcode behind: opcode 3 is reused by
    // S_CMOV_B64 from RDNA3, so the RDNA1 encoding no longer names S_MOV_B32.
    const auto stale = Decode(gfx10Move, Arch::Gfx12, 0);
    REQUIRE(stale.has_value());
    REQUIRE(stale->op == Op::S_CmovB64);
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

TEST_CASE("gfx base ALU batch interprets across generations", "[gfx][alu][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "alu.hsaco";
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

        const auto execution = SR::IL::Interpret(*module);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());

        const auto slot = [&](SR::IL::RegisterClass kind, std::uint32_t index) {
            for (const auto& entry : module->registerSlots)
            {
                if (entry.kind == kind && entry.index == index)
                    return entry.alloca;
            }
            return SR::IL::InvalidValue;
        };

        const auto scalar = [&](std::uint32_t index) {
            const SR::IL::ValueId alloca = slot(SR::IL::RegisterClass::Sgpr, index);
            REQUIRE(alloca != SR::IL::InvalidValue);
            return execution->memory.at(alloca);
        };

        REQUIRE(scalar(0) == 0x0000FFFFu);
        REQUIRE(scalar(1) == 0xFFFF0000u);
        REQUIRE(scalar(2) == 0xFFFF0000u);
        REQUIRE(scalar(3) == 16u);
        REQUIRE(scalar(4) == 0xFFFE0001u);
        REQUIRE(scalar(5) == 0xFFFE0001u);
        REQUIRE(scalar(6) == 0xFFFE0001u);
        REQUIRE(scalar(7) == 0xFFFFFFFEu);
        REQUIRE(scalar(8) == 0xFFFFFE00u);
        REQUIRE(scalar(9) == 0xFFFFFFFFu);

        const SR::IL::ValueId v1 = slot(SR::IL::RegisterClass::Vgpr, 1);
        REQUIRE(v1 != SR::IL::InvalidValue);
        REQUIRE(execution->memory.at(v1) == 0xFFFE0001u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx vector integer batch interprets across generations", "[gfx][valu][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "valu.hsaco";
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

        const auto execution = SR::IL::Interpret(*module);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());

        const auto vector = [&](std::uint32_t index) -> std::uint64_t {
            for (const auto& entry : module->registerSlots)
            {
                if (entry.kind == SR::IL::RegisterClass::Vgpr && entry.index == index)
                    return execution->memory.at(entry.alloca);
            }
            return SR::IL::InvalidValue;
        };

        REQUIRE(vector(2) == 5u);
        REQUIRE(vector(3) == 20u);
        REQUIRE(vector(4) == 640u);
        REQUIRE(vector(7) == 0xFFFFFFFCu);
        REQUIRE(vector(8) == 100u);
        REQUIRE(vector(9) == 0xFFFFFFFAu);
        REQUIRE(vector(10) == 5u);
        REQUIRE(vector(11) == 640u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx scalar carry batch interprets across generations", "[gfx][scc][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "scc.hsaco";
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

        const auto execution = SR::IL::Interpret(*module);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());

        const auto scalar = [&](std::uint32_t index) {
            return RegisterValue(*module, *execution, SR::IL::RegisterClass::Sgpr, index);
        };

        REQUIRE(scalar(1) == 0xFFFFFFFEu);
        REQUIRE(scalar(2) == 1u);
        REQUIRE(scalar(4) == 10u);
        REQUIRE(scalar(5) == 0u);
        REQUIRE(scalar(6) == 0x00000006u);
        REQUIRE(scalar(7) == 1u);
        REQUIRE(scalar(8) == 0xFFFFFFFAu);
        REQUIRE(scalar(9) == 0u);
        REQUIRE(scalar(10) == 4u);
        REQUIRE(scalar(11) == 1u);
        REQUIRE(scalar(12) == 0xFFFFFFF7u);
        REQUIRE(scalar(13) == 2u);
        REQUIRE(scalar(14) == 8u);
        REQUIRE(scalar(15) == 0u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx scalar bit operations interpret across generations", "[gfx][bitops][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "bitops.hsaco";
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

        const auto execution = SR::IL::Interpret(*module);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());

        const auto scalar = [&](std::uint32_t index) {
            return RegisterValue(*module, *execution, SR::IL::RegisterClass::Sgpr, index);
        };

        // RDNA1/2 expose S_FF1/S_FLBIT; RDNA3+ dropped them in favour of the
        // vector CLZ/CTZ forms exercised by the vector test.
        if (testCase.arch == SR::Frontend::Gfx::Arch::Gfx10)
        {
            REQUIRE(scalar(1) == 8u);
            REQUIRE(scalar(2) == 16u);
            REQUIRE(scalar(4) == 0xFFFFFFFFu);
            REQUIRE(scalar(5) == 0xFFFFFFFFu);
        }
        REQUIRE(scalar(6) == 0x0000FF00u);
        REQUIRE(scalar(7) == 0xFFFF00FFu);
        REQUIRE(scalar(8) == 8u);
        REQUIRE(scalar(9) == 1u);
        REQUIRE(scalar(10) == 0u);
        REQUIRE(scalar(11) == 0u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx vector bit operations interpret across generations", "[gfx][vbits][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "vbits.hsaco";
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

        const auto execution = SR::IL::Interpret(*module);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());

        const auto vector = [&](std::uint32_t index) {
            return RegisterValue(*module, *execution, SR::IL::RegisterClass::Vgpr, index);
        };

        REQUIRE(vector(1) == 16u);
        REQUIRE(vector(2) == 8u);
        REQUIRE(vector(4) == 0xFFFFFFFFu);
        REQUIRE(vector(5) == 0xFFFFFFFFu);
        REQUIRE(vector(6) == 8u);
        REQUIRE(vector(8) == 13u);
        REQUIRE(vector(9) == 16u);
        REQUIRE(vector(10) == 13u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx scalar and global loads feed a store", "[gfx][load][test-abi]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "loadstore.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        if (!module.has_value())
            INFO(SR::ToString(module.error()));
        REQUIRE(module.has_value());
        REQUIRE(module->testAbi);
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        SR::IL::ExecutionOptions options;
        options.testBuffer = {{0, 6}, {1, 7}};
        const auto execution = SR::IL::Interpret(*module, options);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 6u);
        REQUIRE(execution->buffer.at(1) == 7u);
        REQUIRE(execution->buffer.at(2) == 42u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx structured emission lowers loops without a dispatcher", "[gfx][structured][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
        {"gfx10", SR::Frontend::Gfx::Arch::Gfx10},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "loop.hsaco";
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
        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto emission = SR::Backend::Reemit(*module);
        if (!emission.has_value())
            INFO(SR::ToString(emission.error()));
        REQUIRE(emission.has_value());
        REQUIRE(HasOpcode(emission->words, kSpirvOpLoopMerge));
        REQUIRE(HasOpcode(emission->words, kSpirvOpPhi));
        REQUIRE_FALSE(HasOpcode(emission->words, kSpirvOpSwitch));
    }
#endif
}

TEST_CASE("gfx structured emission lowers conditionals without a dispatcher", "[gfx][structured][if]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "branch.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        if (!module.has_value())
            INFO(SR::ToString(module.error()));
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 7u);

        const auto emission = SR::Backend::Reemit(*module);
        if (!emission.has_value())
            INFO(SR::ToString(emission.error()));
        REQUIRE(emission.has_value());
        REQUIRE(HasOpcode(emission->words, kSpirvOpSelectionMerge));
        REQUIRE_FALSE(HasOpcode(emission->words, kSpirvOpSwitch));
    }
#endif
}

TEST_CASE("gfx floating point and carry VALU interprets across generations", "[gfx][fpalu][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "fpalu.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        if (!module.has_value())
            INFO(SR::ToString(module.error()));
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        if (!execution.has_value())
            INFO(SR::ToString(execution.error()));
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 7u);
        REQUIRE(execution->buffer.at(1) == 2u);
        REQUIRE(execution->buffer.at(2) == 0x08000000u);
        REQUIRE(execution->buffer.at(3) == 1u);
        REQUIRE(execution->buffer.at(4) == 0xFFFFFFFFu);
        REQUIRE(execution->buffer.at(5) == 0u);
        REQUIRE(execution->buffer.at(6) == 1u);
        REQUIRE(execution->buffer.at(7) == 2u);
        REQUIRE(execution->buffer.at(8) == 1u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx ISA interpreter matches the IL interpreter", "[gfx][isa][divergence]")
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
        for (const char* fixture : {"basic", "loop", "alu", "valu", "scc", "bitops", "vbits", "fpalu",
                                    "loadstore", "branch", "store", "modifiers", "bitfield", "b64",
                                    "compares", "vop1", "vop2", "vop3", "f16", "f64", "cmpwide", "packed",
                                    "sop64", "lds", "flatmem", "buftest", "smem", "sopscalar", "sopctl", "vcmp64", "vclass", "vop3ext", "vdot", "vfp8", "vfp8d", "vmore", "frexp"})
        {
            INFO(testCase.directory << "/" << fixture);
            const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory /
                                               (std::string(fixture) + ".hsaco");
            if (!std::filesystem::exists(path))
                continue;

            const auto bytes = ReadFile(path);
            REQUIRE_FALSE(bytes.empty());

            SR::Frontend::MasterReader reader;
            const auto shader = reader.Read(bytes);
            REQUIRE(shader.has_value());

            auto module = SR::Frontend::Gfx::Lift(*shader,
                                                 testCase.arch,
                                                 "main",
                                                 SR::Frontend::Gfx::LiftOptions{true});
            const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
            INFO(moduleError);
            REQUIRE(module.has_value());

            const std::unordered_map<std::uint32_t, std::uint32_t> initialBuffer = {{0, 6}, {1, 7}};
            SR::IL::ExecutionOptions options;
            options.testBuffer = initialBuffer;
            const auto execution = SR::IL::Interpret(*module, options);
            const std::string executionError =
                execution.has_value() ? std::string() : SR::ToString(execution.error());
            INFO(executionError);
            REQUIRE(execution.has_value());

            std::vector<std::uint32_t> words(shader->rawBytecode.size() / 4);
            std::memcpy(words.data(), shader->rawBytecode.data(), words.size() * sizeof(std::uint32_t));

            SR::Frontend::Gfx::IsaState state;
            state.buffer = initialBuffer;
            const auto isa = SR::Frontend::Gfx::RunIsa(words, testCase.arch, state);
            const std::string isaError = isa.has_value() ? std::string() : SR::ToString(isa.error());
            INFO(isaError);
            REQUIRE(isa.has_value());

            for (const auto& slot : module->registerSlots)
            {
                const std::uint64_t ilValue = execution->memory.at(slot.alloca);
                std::uint64_t isaValue = 0;
                switch (slot.kind)
                {
                case SR::IL::RegisterClass::Sgpr:
                    isaValue = isa->state.sgpr.at(slot.index);
                    break;
                case SR::IL::RegisterClass::Vgpr:
                    isaValue = isa->state.vgpr.at(slot.index);
                    break;
                case SR::IL::RegisterClass::Scc:
                    isaValue = isa->state.scc ? 1u : 0u;
                    break;
                case SR::IL::RegisterClass::Vcc:
                    isaValue = isa->state.vcc ? 1u : 0u;
                    break;
                case SR::IL::RegisterClass::Exec:
                    isaValue = isa->state.exec ? 1u : 0u;
                    break;
                }
                INFO("register class " << static_cast<int>(slot.kind) << " index " << slot.index);
                REQUIRE(ilValue == isaValue);
            }

            REQUIRE(execution->buffer == isa->state.buffer);
        }
    }
#endif
}

TEST_CASE("gfx VOP3 source and output modifiers interpret", "[gfx][modifiers][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "modifiers.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
        INFO(moduleError);
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        const std::string executionError =
            execution.has_value() ? std::string() : SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 6u);
        REQUIRE(execution->buffer.at(1) == 2u);
        REQUIRE(execution->buffer.at(2) == 0u);
        REQUIRE(execution->buffer.at(3) == 4u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx bitfield operations interpret across generations", "[gfx][bitfield][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "bitfield.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
        INFO(moduleError);
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        const std::string executionError =
            execution.has_value() ? std::string() : SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 0x56u);
        REQUIRE(execution->buffer.at(1) == 0xFFFFFFFFu);
        REQUIRE(execution->buffer.at(2) == 0x5600u);
        REQUIRE(execution->buffer.at(3) == 0x81234567u);
        REQUIRE(execution->buffer.at(4) == 0x78123456u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx 64-bit scalar operations interpret across generations", "[gfx][b64][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "b64.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
        INFO(moduleError);
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        const std::string executionError =
            execution.has_value() ? std::string() : SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 0x89ABCDEFu);
        REQUIRE(execution->buffer.at(1) == 0x01234567u);
        REQUIRE(execution->buffer.at(2) == 0x76543210u);
        REQUIRE(execution->buffer.at(3) == 0xFEDCBA98u);
        REQUIRE(execution->buffer.at(4) == 0u);
        REQUIRE(execution->buffer.at(5) == 0u);
        REQUIRE(execution->buffer.at(6) == 0xFFFFFFFFu);
        REQUIRE(execution->buffer.at(7) == 0x89ABCDEFu);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx compare predicates and CMPX EXEC masking interpret", "[gfx][compare][exec]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "compares.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
        INFO(moduleError);
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        const std::string executionError =
            execution.has_value() ? std::string() : SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());
        REQUIRE(execution->buffer.at(0) == 1u);
        REQUIRE(execution->buffer.at(1) == 0u);
        REQUIRE(execution->buffer.at(2) == 1u);
        REQUIRE(execution->buffer.at(3) == 1u);
        REQUIRE(execution->buffer.at(4) == 1u);
        REQUIRE(execution->buffer.at(5) == 0u);
        REQUIRE(execution->buffer.at(6) == 9u);
        REQUIRE(execution->buffer.at(7) == 8u);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx VOP1 conversions and unary math interpret", "[gfx][vop1][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path =
        std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "vop1.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("vop1 fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(0) == 0u);
    REQUIRE(execution->buffer.at(1) == 2u);
    REQUIRE(execution->buffer.at(2) == 3u);
    REQUIRE(execution->buffer.at(3) == 3u);
    REQUIRE(execution->buffer.at(4) == 2u);
    REQUIRE(execution->buffer.at(5) == 51u);
    REQUIRE(execution->buffer.at(6) == 3u);
    REQUIRE(execution->buffer.at(7) == 2u);
    REQUIRE(execution->buffer.at(8) == 0xFFFFFFFFu);
    REQUIRE(execution->buffer.at(9) == 65535u);
    REQUIRE(execution->buffer.at(10) == 24u);
    REQUIRE(execution->buffer.at(11) == 0xFFFFFFFFu);
    REQUIRE(execution->buffer.at(12) == 0x80000000u);
    REQUIRE(execution->buffer.at(13) == 0x40300000u);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx VOP2 multiply and carry family interprets", "[gfx][vop2][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "vop2.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("vop2 fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());
    REQUIRE(execution->buffer.at(0) == 0xFFFFFEu);
    REQUIRE(execution->buffer.at(1) == 0xFFFFFEu);
    REQUIRE(execution->buffer.at(2) == 0u);
    REQUIRE(execution->buffer.at(3) == 0x3FFFu);
    REQUIRE(execution->buffer.at(4) == 0xFFFFFFFDu);
    REQUIRE(execution->buffer.at(5) == 0u);
    REQUIRE(execution->buffer.at(6) == 0xF00FF00Fu);
    REQUIRE(execution->buffer.at(7) == 0x20u);
    REQUIRE(execution->buffer.at(8) == 0x10u);
    REQUIRE(execution->buffer.at(9) == 9u);
    REQUIRE(execution->buffer.at(10) == 2u);
    REQUIRE(execution->buffer.at(11) == 0xFFFFFFFEu);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx VOP3 3-input integer operations interpret", "[gfx][vop3][pipeline]")
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
        {"gfx1200", SR::Frontend::Gfx::Arch::Gfx12},
    };

    const std::uint32_t expected[] = {10, 32, 3, 7, 4, 42, 42, 8, 2, 3, 5, 1, 17, 4, 12, 8, 2};

    for (const Case& testCase : cases)
    {
        INFO(testCase.directory);
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / testCase.directory / "vop3.hsaco";
        if (!std::filesystem::exists(path))
            continue;

        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             testCase.arch,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
        INFO(moduleError);
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        const std::string executionError =
            execution.has_value() ? std::string() : SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());
        for (std::uint32_t index = 0; index < 17; ++index)
            REQUIRE(execution->buffer.at(index) == expected[index]);

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx F16 arithmetic and conversions interpret", "[gfx][f16][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "f16.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("f16 fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x4200u, 0x3C00u, 0x4400u, 0x4500u, 0x4500u, 0x3C00u,
                                      0x4000u, 0x3E00u, 0x3F800000u, 0x4200u, 3u, 0xBC00u,
                                      0xFFFFFFFFu, 0x3800u, 0x4000u, 0x4400u, 0x4000u, 0x4000u,
                                      0x4200u, 0xC000u, 0x4000u, 0x4800u, 32767u, 65535u};
    for (std::uint32_t index = 0; index < 24; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx F64 arithmetic and conversions interpret", "[gfx][f64][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "f64.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("f64 fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x400C0000u, 0x40100000u, 0x40160000u, 0x40080000u, 3u,
                                      0x3FF80000u, 0x3FC00000u, 0x40000000u, 0x40180000u};
    for (std::uint32_t index = 0; index < 9; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx 16-bit and 64-bit compares interpret", "[gfx][compare][wide]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "cmpwide.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("cmpwide fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {1u, 0u, 1u, 1u, 0u, 1u, 1u, 1u, 1u, 1u, 1u, 7u, 8u};
    for (std::uint32_t index = 0; index < 13; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx packed VOP3P arithmetic interprets", "[gfx][packed][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "packed.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("packed fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x46004400u, 0x48004200u, 0x40003C00u, 0x44004200u,
                                      0x49004400u, 0x00080006u, 0xFFFEFFFEu, 0x000F0008u,
                                      0x00030002u, 0x00050004u, 0x00280010u, 0x0000FFFEu,
                                      0x0012000Au};
    for (std::uint32_t index = 0; index < 13; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx scalar 64-bit and bit-scan SOP interprets", "[gfx][sop64][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "sop64.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("sop64 fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0xFFFFFF80u, 0x00000080u, 0x80000001u, 0x01000000u, 0x10u,
                                      0x3Du,       0x03u,       0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu,
                                      0x30u,       0x00u,       0x0Cu,       0x00u,       0x06u,
                                      0x00u,       0x00FFFF00u, 0x00u,       0x000000FFu, 0xFFFFFFFFu,
                                      0x01u,       0x01u,       0x01u,       0x00u,       0x01u};
    for (std::uint32_t index = 0; index < 25; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx data-share loads, stores and atomics interpret", "[gfx][lds][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "lds.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("lds fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x11223344u, 0x55667788u, 5u,          8u,          5u,
                                      10u,         0u,          0xCCu,       0xFFFFFFCCu, 0xAABBu,
                                      0xFFFFAABBu, 0xAABB1122u, 2u,          1u,          2u,
                                      2u,          0xA1u,       0xA4u};
    for (std::uint32_t index = 0; index < 18; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx flat and global memory access interprets", "[gfx][flat][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "flatmem.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("flatmem fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x11223344u, 0x55667788u, 51u,  51u, 4386u, 4386u,
                                      5u,          8u,          8u,   8u,  8u,    0xFFu,
                                      2u,          1u,          0u,   0u,  12u,   15u};
    for (std::uint32_t index = 0; index < 18; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx buffer memory access interprets", "[gfx][buffer][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "buftest.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("buftest fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x11223344u, 0x55667788u, 51u, 4386u, 5u, 8u};
    for (std::uint32_t index = 0; index < 6; ++index)
        REQUIRE(execution->buffer.at(index) == expected[index]);

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx scalar memory loads and widths interpret", "[gfx][smem][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "smem.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("smem fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u,
                                      0u,          0u,          0u,          0u,
                                      0u,          0u,          0u,          0u,
                                      0u,          0u,          0u,          0u,
                                      0x11223344u, 0x55667788u, 0xDDEEFF00u, 51u,
                                      51u,         4386u,       0xDDEEFF00u};
    for (std::uint32_t index = 0; index < 23; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx scalar compare, float and conversion ops interpret", "[gfx][sopscalar][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "sopscalar.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("sopscalar fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {1u,          0u,          1u,          0u,          1u,
                                      0x40800000u, 0x40700000u, 0x3FC00000u, 0x40200000u, 0x40A80000u,
                                      0x4400u,     0x44442222u, 11u,         9u,          3u,
                                      1u,          0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u,
                                      0xC0A00000u, 2u,          0x3E00u,     0x3FC00000u, 0x3FC00000u};
    for (std::uint32_t index = 0; index < 25; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx scalar control and immediate ops interpret", "[gfx][sopctl][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "sopctl.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("sopctl fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {20u, 3u, 0u};
    for (std::uint32_t index = 0; index < 3; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx VOP3-encoded compares write a lane mask", "[gfx][vcmp64][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "vcmp64.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("vcmp64 fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0xFFFFFFFFu, 0u, 0xFFFFFFFFu, 0u, 0xFFFFFFFFu,
                                      0xFFFFFFFFu, 0u, 0xFFFFFFFFu, 0u, 7u};
    for (std::uint32_t index = 0; index < 10; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx class compares, swaps and saturate pack interpret", "[gfx][vclass][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "vclass.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("vclass fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {1u, 1u, 1u, 1u, 1u, 1u, 1u, 0u, 0u,
                                      0x33334444u, 0x11112222u, 255u, 0u};
    for (std::uint32_t index = 0; index < 13; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        INFO("index " << index);
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());

    const auto pipelined = SR::IL::Interpret(*module);
    const std::string pipelinedError =
        pipelined.has_value() ? std::string() : SR::ToString(pipelined.error());
    INFO(pipelinedError);
    REQUIRE(pipelined.has_value());
    for (std::uint32_t index = 0; index < 13; ++index)
    {
        const auto it = pipelined->buffer.find(index);
        const std::uint32_t actual = it == pipelined->buffer.end() ? 0u : it->second;
        INFO("pipelined index " << index);
        REQUIRE(actual == expected[index]);
    }
#endif
}

TEST_CASE("gfx extended VOP3 ALU and conversion forms interpret", "[gfx][vop3ext][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "vop3ext.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("vop3ext fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {6u,          0xFFFEu,     8u,          2u,          4u,
                                      8u,          0xFFFFu,     15u,         92u,         3u,
                                      3u,          5u,          9u,          3u,          0x3F800000u,
                                      0x40200000u, 0xBF800000u, 0x3F800000u, 0xFFFE0002u, 0x00040002u,
                                      0x7FFF7FFFu, 0x80017FFFu, 0x2E663E00u, 0x40003C00u, 16u,
                                      0x00100000u, 2u,          0x0306090Cu, 2u,          0x40A00000u,
                                      0x3F800000u, 0xBF800000u, 0x40200000u, 0x3F800000u, 0x7F800000u};
    for (std::uint32_t index = 0; index < 35; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        INFO("index " << index);
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx VOP3P dot products interpret", "[gfx][vdot][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "vdot.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("vdot fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {0x41480000u, 0x40800000u, 10u, 10u, 8u};
    for (std::uint32_t index = 0; index < 5; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx 8-bit float conversions and dots interpret", "[gfx][vfp8][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    struct Case
    {
        const char* fixture;
        std::vector<std::uint32_t> expected;
    };
    const Case cases[] = {
        {"vfp8", {1071644672u, 1069547520u, 1071644672u, 1072693248u, 1069547520u, 1071644672u}},
        {"vfp8d", {1094598656u, 1091108864u}},
    };

    for (const Case& testCase : cases)
    {
        const std::filesystem::path path =
            std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / (std::string(testCase.fixture) + ".hsaco");
        if (!std::filesystem::exists(path))
            continue;

        INFO(testCase.fixture);
        const auto bytes = ReadFile(path);
        REQUIRE_FALSE(bytes.empty());

        SR::Frontend::MasterReader reader;
        const auto shader = reader.Read(bytes);
        REQUIRE(shader.has_value());

        auto module = SR::Frontend::Gfx::Lift(*shader,
                                             SR::Frontend::Gfx::Arch::Gfx12,
                                             "main",
                                             SR::Frontend::Gfx::LiftOptions{true});
        const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
        INFO(moduleError);
        REQUIRE(module.has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());

        const auto execution = SR::IL::Interpret(*module);
        const std::string executionError =
            execution.has_value() ? std::string() : SR::ToString(execution.error());
        INFO(executionError);
        REQUIRE(execution.has_value());

        for (std::size_t index = 0; index < testCase.expected.size(); ++index)
        {
            const auto it = execution->buffer.find(static_cast<std::uint32_t>(index));
            const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
            INFO("index " << index);
            REQUIRE(actual == testCase.expected[index]);
        }

        REQUIRE(SR::IL::RunPipeline(*module).has_value());
        REQUIRE(SR::IL::VerifyModule(*module).has_value());
        REQUIRE(SR::Backend::Reemit(*module).has_value());
    }
#endif
}

TEST_CASE("gfx VOP3 mask, byte pack, SAD16 and 64-bit shift interpret", "[gfx][vmore][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "vmore.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("vmore fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {1u, 0x00020000u, 13u, 0x08000000u, 0x08000000u};
    for (std::uint32_t index = 0; index < 5; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
#endif
}

TEST_CASE("gfx frexp splits mantissa and exponent", "[gfx][frexp][pipeline]")
{
#if !defined(SR_CORPUS_ROOT)
    SUCCEED("SR_CORPUS_ROOT is not defined");
#else
    const std::filesystem::path path = std::filesystem::path(SR_CORPUS_ROOT) / "gfx1200" / "frexp.hsaco";
    if (!std::filesystem::exists(path))
    {
        SUCCEED("frexp fixture is not present");
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
    const std::string moduleError = module.has_value() ? std::string() : SR::ToString(module.error());
    INFO(moduleError);
    REQUIRE(module.has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());

    const auto execution = SR::IL::Interpret(*module);
    const std::string executionError =
        execution.has_value() ? std::string() : SR::ToString(execution.error());
    INFO(executionError);
    REQUIRE(execution.has_value());

    const std::uint32_t expected[] = {1u,          0x3F400000u, 0u,          0x3F400000u, 0u,
                                      0u,          0u,          0xFFFFFF6Cu, 0x3F000000u};
    for (std::uint32_t index = 0; index < 9; ++index)
    {
        const auto it = execution->buffer.find(index);
        const std::uint32_t actual = it == execution->buffer.end() ? 0u : it->second;
        INFO("index " << index);
        REQUIRE(actual == expected[index]);
    }

    REQUIRE(SR::IL::RunPipeline(*module).has_value());
    REQUIRE(SR::IL::VerifyModule(*module).has_value());
    REQUIRE(SR::Backend::Reemit(*module).has_value());
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
