#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "SR/Backend/Reemitter.hpp"
#include "SR/Common/Assertions.hpp"
#include "SR/Common/IL/Dump.hpp"
#include "SR/Common/IL/Interpreter.hpp"
#include "SR/Common/IL/Module.hpp"
#include "SR/Common/IL/Passes.hpp"
#include "SR/Common/IL/Verifier.hpp"
#include "SR/Frontend/Arch/Gfx/GfxLifter.hpp"
#include "SR/Frontend/Frontend.hpp"
#include "SR/Frontend/MasterReader.hpp"

namespace {

void PrintUsage()
{
    std::cout << "shader-recomp " << SR::Frontend::LibraryVersion() << '\n'
              << "Usage: shader-recomp [--version] [--help] <input>\n"
              << "       shader-recomp --inspect <input>\n"
              << "       shader-recomp [--arch <name>] [--stage <name>] [--dump-il] --spirv <output.spv> <input>\n"
              << '\n'
              << "Registered readers: spirv, xbox, dxil, dxbc, cubin, hsaco\n"
              << "Architectures: gfx10, gfx1013, gfx1030, gfx1100, gfx1150, gfx1200\n"
              << "An Xbox Series X|S container defaults to gfx1030.\n"
              << "Stages: compute (default for code objects), vertex, fragment.\n"
              << "Options: --test-abi, --wave64, --user-data <d0,d1,...>, --user-data-push <count>, --interpret.\n";
}

std::vector<std::uint8_t> ReadFile(const std::string& path, bool& ok)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        ok = false;
        return {};
    }

    ok = true;
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void PrintShader(const SR::Frontend::DecodedShader& shader)
{
    std::size_t instructionCount = 0;
    for (const auto& function : shader.functions)
        instructionCount += function.instructions.size();

    std::cout << "format:        " << SR::Frontend::ToString(shader.format) << '\n'
              << "architecture:  " << shader.architecture << '\n'
              << "stage:         " << SR::IL::ToString(shader.stage) << '\n'
              << "entry:         " << (shader.entryPoint.empty() ? "<none>" : shader.entryPoint) << '\n'
              << "program ver:   0x" << std::hex << shader.programVersion << std::dec << '\n'
              << "functions:     " << shader.functions.size() << '\n'
              << "instructions:  " << instructionCount << '\n'
              << "raw bytecode:  " << shader.rawBytecode.size() << " bytes\n";

    for (const std::string& note : shader.notes)
        std::cout << "note:          " << note << '\n';
}

} // namespace

int main(int argc, char** argv)
{
    SR::InstallAssertionHandler("shader-recomp");
    std::string input;
    std::string output;
    std::string archName = "gfx1200";
    bool archSpecified = false;
    std::string stageName;
    bool inspect = false;
    bool recompile = false;
    bool dumpIl = false;
    bool testAbi = false;
    bool wave64 = false;
    bool interpret = false;
    std::vector<std::uint32_t> userData;
    bool userDataFromPush = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--version")
        {
            std::cout << "shader-recomp " << SR::Frontend::LibraryVersion() << '\n';
            return 0;
        }

        if (argument == "--help" || argument == "-h")
        {
            PrintUsage();
            return 0;
        }

        if (argument == "--inspect")
        {
            inspect = true;
            continue;
        }

        if (argument == "--dump-il")
        {
            dumpIl = true;
            continue;
        }

        if (argument == "--test-abi")
        {
            testAbi = true;
            continue;
        }

        if (argument == "--wave64")
        {
            wave64 = true;
            continue;
        }

        if (argument == "--interpret")
        {
            interpret = true;
            continue;
        }

        if (argument == "--arch" && i + 1 < argc)
        {
            archName = argv[++i];
            archSpecified = true;
            continue;
        }

        if (argument == "--stage" && i + 1 < argc)
        {
            stageName = argv[++i];
            continue;
        }

        if (argument == "--user-data" && i + 1 < argc)
        {
            std::string_view text = argv[++i];
            while (!text.empty())
            {
                const std::size_t comma = text.find(',');
                const std::string_view field = text.substr(0, comma);
                userData.push_back(static_cast<std::uint32_t>(std::stoul(std::string(field), nullptr, 0)));
                text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
            }
            continue;
        }

        if (argument == "--user-data-push" && i + 1 < argc)
        {
            const auto count = static_cast<std::size_t>(std::stoul(argv[++i]));
            userData.assign(count, 0u);
            userDataFromPush = true;
            continue;
        }

        if (argument == "--spirv" && i + 1 < argc)
        {
            output = argv[++i];
            recompile = true;
            continue;
        }

        if (argument.size() > 1 && argument[0] == '-')
        {
            std::cerr << "unknown option: " << argument << '\n';
            return 2;
        }

        if (input.empty())
        {
            input = argv[i];
            continue;
        }

        std::cerr << "unexpected argument: " << argument << '\n';
        return 2;
    }

    if (input.empty())
    {
        PrintUsage();
        return (inspect || recompile) ? 2 : 0;
    }

    bool ok = false;
    const auto bytes = ReadFile(input, ok);
    if (!ok)
    {
        std::cerr << "cannot open '" << input << "'\n";
        return 1;
    }

    SR::Frontend::MasterReader reader;
    auto shader = reader.Read(bytes);
    if (!shader)
    {
        std::cerr << SR::ToString(shader.error()) << '\n';
        return 1;
    }

    if (!stageName.empty())
    {
        if (stageName == "vertex")
            shader->stage = SR::IL::Stage::Vertex;
        else if (stageName == "fragment" || stageName == "pixel")
            shader->stage = SR::IL::Stage::Fragment;
        else if (stageName == "compute")
            shader->stage = SR::IL::Stage::Compute;
        else
        {
            std::cerr << "unknown stage '" << stageName << "'\n";
            return 2;
        }
    }

    if (!recompile)
    {
        PrintShader(*shader);
        return 0;
    }

    if (shader->format != SR::Frontend::BinaryFormat::Hsaco
        && shader->format != SR::Frontend::BinaryFormat::XboxShader)
    {
        std::cerr << "recompilation currently targets AMDGPU code objects (hsaco) and Xbox Series X|S containers\n";
        return 1;
    }

    // Console programs are GFX10.3; the other formats keep the RDNA4 default.
    if (!archSpecified && shader->format == SR::Frontend::BinaryFormat::XboxShader)
        archName = "gfx1030";

    const auto* arch = SR::Frontend::Gfx::FindArch(archName);
    if (arch == nullptr)
    {
        std::cerr << "unknown architecture '" << archName << "'\n";
        return 2;
    }

    SR::Frontend::Gfx::LiftOptions liftOptions;
    liftOptions.testAbi = testAbi;
    liftOptions.wave64 = wave64;
    liftOptions.userData = userData;
    liftOptions.userDataFromPush = userDataFromPush;
    auto module = SR::Frontend::Gfx::Lift(*shader, arch->arch, "main", liftOptions);
    if (!module)
    {
        std::cerr << SR::ToString(module.error()) << '\n';
        return 1;
    }

    std::vector<SR::Diagnostic> issues;
    const auto translated = SR::IL::VerifyModule(*module, &issues);
    if (!translated)
    {
        std::cerr << "translated IL: " << SR::ToString(translated.error()) << '\n';
        return 1;
    }

    if (const auto pipeline = SR::IL::RunPipeline(*module); !pipeline)
    {
        std::cerr << "pipeline (CFA -> SSA -> optimize): " << SR::ToString(pipeline.error()) << '\n';
        return 1;
    }

    const auto verified = SR::IL::VerifyModule(*module, &issues);
    if (!verified)
    {
        std::cerr << "SSA IL: " << SR::ToString(verified.error()) << '\n';
        return 1;
    }

    if (dumpIl)
        std::cout << SR::IL::DumpModule(*module);

    if (interpret)
    {
        const auto execution = SR::IL::Interpret(*module);
        if (!execution)
        {
            std::cerr << SR::ToString(execution.error()) << '\n';
            return 1;
        }

        std::cout << "buffer:";
        for (std::uint32_t index = 0; index < 64; ++index)
        {
            const auto entry = execution->buffer.find(index);
            if (entry != execution->buffer.end())
                std::cout << ' ' << index << '=' << entry->second;
        }
        std::cout << '\n';
    }

    const auto emission = SR::Backend::Reemit(*module);
    if (!emission)
    {
        std::cerr << SR::ToString(emission.error()) << '\n';
        return 1;
    }

    std::ofstream file(output, std::ios::binary);
    if (!file)
    {
        std::cerr << "cannot write '" << output << "'\n";
        return 1;
    }

    file.write(reinterpret_cast<const char*>(emission->words.data()),
               static_cast<std::streamsize>(emission->words.size() * sizeof(std::uint32_t)));
    if (!file)
    {
        std::cerr << "failed writing '" << output << "'\n";
        return 1;
    }

    std::cout << "wrote " << output << ": " << emission->words.size() << " SPIR-V words\n";
    return 0;
}
