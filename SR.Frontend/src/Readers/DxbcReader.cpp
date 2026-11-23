#include "SR/Frontend/Readers/DxbcReader.hpp"

#include <string>
#include <utility>
#include <vector>

#include "SR/Common/ByteReader.hpp"
#include "SR/Frontend/Container/DxbcContainer.hpp"

// Token layout follows d3d11TokenizedProgramFormat.hpp from the Windows Driver
// Kit. See opcode token bits [10:00], [30:24] and the extended-token chain at
// bit 31.
namespace SR::Frontend {
namespace {

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

IL::Stage StageFromDxbcType(std::uint32_t type) noexcept
{
    switch (type)
    {
    case 0:
        return IL::Stage::Fragment;
    case 1:
        return IL::Stage::Vertex;
    case 2:
        return IL::Stage::Geometry;
    case 3:
        return IL::Stage::TessControl;
    case 4:
        return IL::Stage::TessEval;
    case 5:
        return IL::Stage::Compute;
    default:
        return IL::Stage::Unknown;
    }
}

Status DecodeProgram(std::span<const std::uint8_t> code, DecodedShader& shader)
{
    if (code.size() < 8)
        return std::unexpected(MakeError("SR-DXBC-007", "truncated program header", 0));

    ByteReader reader(code);

    const auto version = reader.readU32();
    const auto length = reader.readU32();
    if (!version || !length)
        return std::unexpected(MakeError("SR-DXBC-007", "truncated program header", reader.offset()));

    shader.programVersion = *version;
    shader.shaderModel = *version & 0xffffu;
    shader.stage = StageFromDxbcType((*version >> 16) & 0xffffu);

    if (*length < 2)
        return std::unexpected(MakeError("SR-DXBC-008", "invalid program length", 4));

    const std::uint64_t programBytes = static_cast<std::uint64_t>(*length) * 4;
    if (programBytes > code.size())
        return std::unexpected(MakeError("SR-DXBC-008", "program length exceeds code chunk", 4));

    const std::uint32_t instructionDwords = *length - 2;
    std::uint32_t consumed = 0;

    DecodedFunction function;
    function.name = "main";

    while (consumed < instructionDwords)
    {
        const auto pc = static_cast<std::uint32_t>(reader.offset());
        const auto token = reader.readU32();
        if (!token)
            return std::unexpected(MakeError("SR-DXBC-009", "truncated instruction token", pc));

        ++consumed;

        std::uint32_t opcode = *token & 0x7ffu;
        std::uint32_t tokenCount = (*token >> 24) & 0x7fu;
        std::uint32_t prefixCount = 1;

        std::vector<std::uint32_t> raw;
        raw.push_back(*token);

        if (opcode == 0x35u)
        {
            // Custom-data blocks carry their length in the following DWORD.
            const auto customLength = reader.readU32();
            if (!customLength)
                return std::unexpected(MakeError("SR-DXBC-009", "truncated custom-data length", pc));

            ++consumed;
            prefixCount = 2;
            tokenCount = *customLength;
            raw.push_back(*customLength);
        }
        else if ((*token & 0x80000000u) != 0u)
        {
            // Extended opcode tokens are chained; the instruction length
            // includes every extended token.
            bool more = true;
            while (more)
            {
                const auto extended = reader.readU32();
                if (!extended)
                    return std::unexpected(MakeError("SR-DXBC-009", "truncated extended opcode token", pc));

                ++consumed;
                ++prefixCount;
                raw.push_back(*extended);
                more = (*extended & 0x80000000u) != 0u;
            }
        }

        if (tokenCount == 0)
        {
            return std::unexpected(MakeError(
                "SR-DXBC-010",
                "instruction length is not encoded; SM4 bytecode is not supported",
                pc));
        }

        if (tokenCount < prefixCount || consumed + (tokenCount - prefixCount) > instructionDwords)
            return std::unexpected(MakeError("SR-DXBC-009", "instruction overlaps program end", pc));

        raw.reserve(tokenCount);
        for (std::uint32_t i = prefixCount; i < tokenCount; ++i)
        {
            const auto operand = reader.readU32();
            if (!operand)
                return std::unexpected(MakeError("SR-DXBC-009", "truncated instruction operand", reader.offset() - 4));

            raw.push_back(*operand);
            ++consumed;
        }

        DecodedInstruction instruction;
        instruction.pc = pc;
        instruction.opcode = static_cast<std::uint16_t>(opcode);
        instruction.rawTokens = std::move(raw);
        function.instructions.push_back(std::move(instruction));
    }

    shader.functions.push_back(std::move(function));
    return {};
}

} // namespace

BinaryFormat DxbcReader::Format() const
{
    return BinaryFormat::Dxbc;
}

std::string_view DxbcReader::Name() const
{
    return "dxbc";
}

bool DxbcReader::Probe(std::span<const std::uint8_t> bytes) const
{
    const auto container = DxbcContainer::Parse(bytes);
    return container.has_value() && container->IsTokenized() && !container->IsDxil();
}

Result<DecodedShader> DxbcReader::Read(std::span<const std::uint8_t> bytes) const
{
    const auto container = DxbcContainer::Parse(bytes);
    if (!container)
        return std::unexpected(container.error());

    if (container->IsDxil())
        return std::unexpected(MakeError("SR-DXBC-011", "container carries DXIL, not tokenized DXBC", 0));

    const DxbcChunk* code = container->CodeChunk();
    if (!code)
        return std::unexpected(MakeError("SR-DXBC-012", "missing SHDR/SHEX/SHAB code chunk", 0));

    DecodedShader shader;
    shader.format = BinaryFormat::Dxbc;
    shader.architecture = "dxbc";
    shader.entryPoint = "main";

    const Status decoded = DecodeProgram(code->data, shader);
    if (!decoded)
    {
        Diagnostic diagnostic = decoded.error();
        diagnostic.offset += code->offset + 8;
        return std::unexpected(std::move(diagnostic));
    }

    return shader;
}

} // namespace SR::Frontend
