#include "SR/Frontend/Readers/CubinReader.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SR/Common/ByteReader.hpp"
#include "SR/Frontend/Container/ElfContainer.hpp"

namespace SR::Frontend {
namespace {

constexpr std::uint16_t kElfMachineCuda = 190;
constexpr std::size_t kSassInstructionBytes = 16;

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

std::string FunctionName(std::string_view sectionName)
{
    constexpr std::string_view prefix = ".text.";
    if (sectionName == ".text")
        return "text";
    if (sectionName.size() > prefix.size() && sectionName.substr(0, prefix.size()) == prefix)
        return std::string(sectionName.substr(prefix.size()));
    return std::string(sectionName);
}

} // namespace

BinaryFormat CubinReader::Format() const
{
    return BinaryFormat::Cubin;
}

std::string_view CubinReader::Name() const
{
    return "cubin";
}

bool CubinReader::Probe(std::span<const std::uint8_t> bytes) const
{
    const auto container = ElfContainer::Parse(bytes);
    return container.has_value() && container->machine() == kElfMachineCuda;
}

Result<DecodedShader> CubinReader::Read(std::span<const std::uint8_t> bytes) const
{
    const auto container = ElfContainer::Parse(bytes);
    if (!container)
        return std::unexpected(container.error());

    if (container->machine() != kElfMachineCuda)
        return std::unexpected(MakeError("SR-CUBIN-001", "ELF is not a CUDA cubin", 0));

    DecodedShader shader;
    shader.format = BinaryFormat::Cubin;
    shader.architecture = "sass";

    for (const auto& section : container->sections())
    {
        if (section.name != ".text" && section.name.rfind(".text.", 0) != 0)
            continue;

        DecodedFunction function;
        function.name = FunctionName(section.name);

        ByteReader reader(section.data);
        std::size_t offset = 0;
        while (offset < section.data.size())
        {
            DecodedInstruction instruction;
            instruction.pc = static_cast<std::uint32_t>(offset);
            instruction.opcode = 0; // SASS opcode decoding lives in the remapper.
            instruction.rawTokens.reserve(kSassInstructionBytes / 4);

            for (std::size_t word = 0; word < kSassInstructionBytes / 4; ++word)
            {
                if (offset + (word + 1) * 4 <= section.data.size())
                {
                    const auto value = reader.readU32();
                    instruction.rawTokens.push_back(value.value_or(0));
                }
                else
                {
                    instruction.rawTokens.push_back(0);
                }
            }

            function.instructions.push_back(std::move(instruction));
            offset += kSassInstructionBytes;
        }

        shader.rawBytecode.insert(shader.rawBytecode.end(), section.data.begin(), section.data.end());
        shader.functions.push_back(std::move(function));
    }

    if (shader.functions.empty())
        return std::unexpected(MakeError("SR-CUBIN-002", "cubin contains no .text sections", 0));

    return shader;
}

} // namespace SR::Frontend
