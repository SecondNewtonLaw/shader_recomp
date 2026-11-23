#include "SR/Frontend/Readers/SpirvReader.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "SR/Common/ByteReader.hpp"

// SPIR-V binary format, Khronos SPIR-V specification: five-word header followed
// by instructions of (word count, opcode, operands).
namespace SR::Frontend {
namespace {

constexpr std::uint32_t kSpirvMagic = 0x07230203u;
constexpr std::uint32_t kHeaderWords = 5;
constexpr std::uint16_t kOpEntryPoint = 15;

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

IL::Stage StageFromExecutionModel(std::uint32_t model) noexcept
{
    switch (model)
    {
    case 0:
        return IL::Stage::Vertex;
    case 1:
        return IL::Stage::TessControl;
    case 2:
        return IL::Stage::TessEval;
    case 3:
        return IL::Stage::Geometry;
    case 4:
        return IL::Stage::Fragment;
    case 5:
    case 6: // OpenCL kernel
        return IL::Stage::Compute;
    case 5266:
    case 5364:
        return IL::Stage::Task;
    case 5267:
    case 5365:
        return IL::Stage::Mesh;
    case 5313:
        return IL::Stage::RayGeneration;
    case 5314:
        return IL::Stage::Intersection;
    case 5315:
        return IL::Stage::AnyHit;
    case 5316:
        return IL::Stage::ClosestHit;
    case 5317:
        return IL::Stage::Miss;
    case 5318:
        return IL::Stage::Callable;
    default:
        return IL::Stage::Unknown;
    }
}

std::string DecodeString(std::span<const std::uint32_t> words, std::size_t first)
{
    std::string text;
    for (std::size_t i = first; i < words.size(); ++i)
    {
        const std::uint32_t word = words[i];
        for (int byte = 0; byte < 4; ++byte)
        {
            const char c = static_cast<char>((word >> (byte * 8)) & 0xffu);
            if (c == '\0')
                return text;
            text.push_back(c);
        }
    }

    return text;
}

} // namespace

BinaryFormat SpirvReader::Format() const
{
    return BinaryFormat::Spirv;
}

std::string_view SpirvReader::Name() const
{
    return "spirv";
}

bool SpirvReader::Probe(std::span<const std::uint8_t> bytes) const
{
    return bytes.size() >= kHeaderWords * 4 &&
           std::memcmp(bytes.data(), &kSpirvMagic, sizeof(kSpirvMagic)) == 0;
}

Result<DecodedShader> SpirvReader::Read(std::span<const std::uint8_t> bytes) const
{
    if (!Probe(bytes))
        return std::unexpected(MakeError("SR-SPIRV-001", "missing SPIR-V magic", 0));

    if (bytes.size() % 4 != 0)
        return std::unexpected(MakeError("SR-SPIRV-002", "SPIR-V size is not a multiple of 4 bytes", 0));

    ByteReader reader(bytes);
    const auto magic = reader.readU32();
    const auto version = reader.readU32();
    const auto generator = reader.readU32();
    const auto bound = reader.readU32();
    const auto schema = reader.readU32();
    if (!magic || !version || !generator || !bound || !schema)
        return std::unexpected(MakeError("SR-SPIRV-002", "truncated SPIR-V header", reader.offset()));

    if (*magic != kSpirvMagic)
        return std::unexpected(MakeError("SR-SPIRV-001", "invalid SPIR-V magic", 0));

    DecodedShader shader;
    shader.format = BinaryFormat::Spirv;
    shader.architecture = "spirv";
    shader.programVersion = *version;
    shader.rawBytecode.assign(bytes.begin(), bytes.end());

    DecodedFunction function;
    function.name = "module";

    const std::size_t wordCount = bytes.size() / 4;
    std::size_t index = kHeaderWords;

    while (index < wordCount)
    {
        const auto first = reader.readU32();
        if (!first)
            return std::unexpected(MakeError("SR-SPIRV-002", "truncated instruction header", index * 4));

        const std::uint16_t instructionWords = static_cast<std::uint16_t>(*first >> 16);
        const std::uint16_t opcode = static_cast<std::uint16_t>(*first & 0xffffu);
        if (instructionWords == 0 || index + instructionWords > wordCount)
            return std::unexpected(MakeError("SR-SPIRV-002", "instruction exceeds the module", index * 4));

        DecodedInstruction instruction;
        instruction.pc = static_cast<std::uint32_t>(index);
        instruction.opcode = opcode;
        instruction.rawTokens.reserve(instructionWords);
        instruction.rawTokens.push_back(*first);

        for (std::uint16_t i = 1; i < instructionWords; ++i)
        {
            const auto word = reader.readU32();
            if (!word)
                return std::unexpected(MakeError("SR-SPIRV-002", "truncated instruction operands", index * 4));

            instruction.rawTokens.push_back(*word);
        }

        if (opcode == kOpEntryPoint && instruction.rawTokens.size() >= 3 && shader.entryPoint.empty())
        {
            shader.stage = StageFromExecutionModel(instruction.rawTokens[1]);
            shader.entryPoint = DecodeString(instruction.rawTokens, 3);
            function.name = shader.entryPoint.empty() ? "module" : shader.entryPoint;
        }

        function.instructions.push_back(std::move(instruction));
        index += instructionWords;
    }

    shader.functions.push_back(std::move(function));
    return shader;
}

} // namespace SR::Frontend
