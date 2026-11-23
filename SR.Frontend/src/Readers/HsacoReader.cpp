#include "SR/Frontend/Readers/HsacoReader.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SR/Common/ByteReader.hpp"
#include "SR/Frontend/Container/ElfContainer.hpp"

namespace SR::Frontend {
namespace {

constexpr std::uint16_t kElfMachineAmdgpu = 224;

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

} // namespace

BinaryFormat HsacoReader::Format() const
{
    return BinaryFormat::Hsaco;
}

std::string_view HsacoReader::Name() const
{
    return "hsaco";
}

bool HsacoReader::Probe(std::span<const std::uint8_t> bytes) const
{
    const auto container = ElfContainer::Parse(bytes);
    return container.has_value() && container->machine() == kElfMachineAmdgpu;
}

Result<DecodedShader> HsacoReader::Read(std::span<const std::uint8_t> bytes) const
{
    const auto container = ElfContainer::Parse(bytes);
    if (!container)
        return std::unexpected(container.error());

    if (container->machine() != kElfMachineAmdgpu)
        return std::unexpected(MakeError("SR-HSACO-001", "ELF is not an AMDGPU code object", 0));

    DecodedShader shader;
    shader.format = BinaryFormat::Hsaco;
    shader.architecture = "gcn";

    DecodedFunction function;
    function.name = "text";

    for (const auto& section : container->sections())
    {
        if (section.name != ".text" && section.name.rfind(".text.", 0) != 0)
            continue;

        ByteReader reader(section.data);
        std::size_t offset = 0;
        while (offset + 4 <= section.data.size())
        {
            const auto word = reader.readU32();
            if (!word)
                break;

            DecodedInstruction instruction;
            instruction.pc = static_cast<std::uint32_t>(offset);
            instruction.opcode = 0; // GCN/RDNA opcode decoding lives in the remapper.
            instruction.rawTokens.push_back(*word);
            function.instructions.push_back(std::move(instruction));
            offset += 4;
        }

        shader.rawBytecode.insert(shader.rawBytecode.end(), section.data.begin(), section.data.end());
    }

    if (function.instructions.empty())
        return std::unexpected(MakeError("SR-HSACO-002", "code object contains no .text instructions", 0));

    shader.functions.push_back(std::move(function));
    return shader;
}

} // namespace SR::Frontend
