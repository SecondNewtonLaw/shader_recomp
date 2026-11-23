#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "SR/Common/IL/Module.hpp"
#include "SR/Frontend/BinaryFormat.hpp"

namespace SR::Frontend {

enum class OperandKind : std::uint8_t
{
    Register,
    Immediate,
    Relative,
    Label,
    Resource,
    Other,
};

struct DecodedOperand
{
    OperandKind kind = OperandKind::Other;
    std::uint64_t value = 0;
};

struct DecodedInstruction
{
    std::uint32_t pc = 0;
    std::uint16_t opcode = 0;
    std::vector<std::uint32_t> rawTokens;
    std::vector<DecodedOperand> operands;
};

struct DecodedFunction
{
    std::string name;
    std::vector<DecodedInstruction> instructions;
};

struct DecodedShader
{
    BinaryFormat format = BinaryFormat::Unknown;
    IL::Stage stage = IL::Stage::Unknown;
    std::string entryPoint;
    std::string architecture;
    std::uint32_t programVersion = 0;
    std::uint32_t shaderModel = 0;
    std::vector<DecodedFunction> functions;
    std::vector<std::uint8_t> rawBytecode;
};

} // namespace SR::Frontend
