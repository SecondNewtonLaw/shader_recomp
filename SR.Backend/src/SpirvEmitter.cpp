#include "SR/Backend/Reemitter.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spirv-tools/libspirv.h>

#include "SR/Common/IL/Analysis.hpp"

namespace SR::Backend {
namespace {

constexpr std::uint32_t kMagic = 0x07230203u;
constexpr std::uint32_t kVersion = 0x00010600u;

enum SpvOp : std::uint16_t
{
    OpUndef = 1,
    OpExtInstImport = 11,
    OpExtInst = 12,
    OpMemoryModel = 14,
    OpEntryPoint = 15,
    OpExecutionMode = 16,
    OpCapability = 17,
    OpTypeVoid = 19,
    OpTypeBool = 20,
    OpTypeInt = 21,
    OpTypeFloat = 22,
    OpTypeVector = 23,
    OpTypeImage = 25,
    OpTypeSampler = 26,
    OpTypeSampledImage = 27,
    OpTypeMatrix = 24,
    OpTypeRuntimeArray = 29,
    OpTypeArray = 28,
    OpTypeStruct = 30,
    OpTypePointer = 32,
    OpTypeFunction = 33,
    OpConstantTrue = 41,
    OpConstantFalse = 42,
    OpConstant = 43,
    OpFunction = 54,
    OpFunctionEnd = 56,
    OpVariable = 59,
    OpSampledImage = 86,
    OpImageSampleImplicitLod = 87,
    OpImageSampleExplicitLod = 88,
    OpImageRead = 98,
    OpImageWrite = 99,
    OpEmitVertex = 218,
    OpEndPrimitive = 219,
    OpLoad = 61,
    OpStore = 62,
    OpAccessChain = 65,
    OpDecorate = 71,
    OpMemberDecorate = 72,
    OpConvertFToU = 109,
    OpConvertFToS = 110,
    OpConvertSToF = 111,
    OpConvertUToF = 112,
    OpUConvert = 113,
    OpSConvert = 114,
    OpFConvert = 115,
    OpConvertUToPtr = 120,
    OpBitcast = 124,
    OpSNegate = 126,
    OpFNegate = 127,
    OpIAdd = 128,
    OpFAdd = 129,
    OpISub = 130,
    OpFSub = 131,
    OpIMul = 132,
    OpFMul = 133,
    OpUDiv = 134,
    OpSDiv = 135,
    OpFDiv = 136,
    OpUMod = 137,
    OpSRem = 138,
    OpSMod = 139,
    OpFRem = 140,
    OpFMod = 141,
    OpLogicalNot = 168,
    OpLogicalEqual = 164,
    OpLogicalNotEqual = 165,
    OpLogicalOr = 166,
    OpLogicalAnd = 167,
    OpCompositeConstruct = 80,
    OpCompositeExtract = 81,
    OpSelect = 169,
    OpIEqual = 170,
    OpINotEqual = 171,
    OpUGreaterThan = 172,
    OpSGreaterThan = 173,
    OpUGreaterThanEqual = 174,
    OpSGreaterThanEqual = 175,
    OpULessThan = 176,
    OpSLessThan = 177,
    OpULessThanEqual = 178,
    OpSLessThanEqual = 179,
    OpFOrdEqual = 180,
    OpFUnordEqual = 181,
    OpFOrdNotEqual = 182,
    OpFUnordNotEqual = 183,
    OpFOrdLessThan = 184,
    OpFUnordLessThan = 185,
    OpFOrdGreaterThan = 186,
    OpFUnordGreaterThan = 187,
    OpFOrdLessThanEqual = 188,
    OpFUnordLessThanEqual = 189,
    OpFOrdGreaterThanEqual = 190,
    OpFUnordGreaterThanEqual = 191,
    OpShiftRightLogical = 194,
    OpShiftRightArithmetic = 195,
    OpShiftLeftLogical = 196,
    OpBitwiseOr = 197,
    OpBitwiseXor = 198,
    OpBitwiseAnd = 199,
    OpNot = 200,
    OpBitReverse = 204,
    OpBitCount = 205,
    OpPhi = 245,
    OpLoopMerge = 246,
    OpSelectionMerge = 247,
    OpLabel = 248,
    OpGroupNonUniformBroadcastFirst = 338,
    OpGroupNonUniformBallot = 339,
    OpBranch = 249,
    OpBranchConditional = 250,
    OpSwitch = 251,
    OpReturn = 253,
    OpReturnValue = 254,
    OpUnreachable = 255,
};

enum GlslStd450 : std::uint32_t
{
    GlslFAbs = 4,
    GlslRoundEven = 2,
    GlslTrunc = 3,
    GlslFloor = 8,
    GlslCeil = 9,
    GlslFract = 10,
    GlslSin = 13,
    GlslCos = 14,
    GlslFMin = 37,
    GlslFMax = 40,
    GlslFClamp = 43,
    GlslFma = 50,
    GlslExp2 = 29,
    GlslLog2 = 30,
    GlslSqrt = 31,
    GlslInverseSqrt = 32,
    GlslFindILsb = 73,
    GlslFindSMsb = 74,
    GlslFindUMsb = 75,
};

Diagnostic EmitError(std::string code, std::string message, std::uint32_t pc = 0)
{
    return Diagnostic::Error(std::move(code), std::move(message), pc);
}

// Post-dominator sets for one function. The virtual exit has index blockCount.
class PostDominators
{
public:
    explicit PostDominators(const IL::Function& function)
        : m_blockCount(function.blocks.size())
        , m_exit(m_blockCount)
    {
        const std::size_t nodes = m_blockCount + 1;
        m_sets.assign(nodes, std::vector<bool>(nodes, true));
        m_sets[m_exit].assign(nodes, false);
        m_sets[m_exit][m_exit] = true;

        bool changed = true;
        std::size_t rounds = 0;
        while (changed && rounds++ < nodes * 4 + 16)
        {
            changed = false;
            for (IL::BlockId block = 0; block < m_blockCount; ++block)
            {
                std::vector<bool> common(nodes, true);
                for (const IL::BlockId successor : Successors(function, block))
                {
                    for (std::size_t i = 0; i < nodes; ++i)
                        common[i] = common[i] && m_sets[successor][i];
                }
                common[block] = true;
                if (common != m_sets[block])
                {
                    m_sets[block] = std::move(common);
                    changed = true;
                }
            }
        }
    }

    [[nodiscard]] IL::BlockId Immediate(IL::BlockId block) const noexcept
    {
        if (block >= m_blockCount)
            return IL::InvalidBlockId;

        IL::BlockId best = IL::InvalidBlockId;
        std::size_t bestSize = 0;
        for (IL::BlockId candidate = 0; candidate < m_blockCount + 1; ++candidate)
        {
            if (candidate == block || !m_sets[block][candidate])
                continue;
            const std::size_t size = static_cast<std::size_t>(
                std::count(m_sets[candidate].begin(), m_sets[candidate].end(), true));
            if (size > bestSize)
            {
                best = candidate;
                bestSize = size;
            }
        }
        return best;
    }

    [[nodiscard]] IL::BlockId Exit() const noexcept { return m_exit; }

private:
    [[nodiscard]] std::vector<IL::BlockId> Successors(const IL::Function& function, IL::BlockId block) const
    {
        const IL::Terminator& terminator = function.blocks[block].terminator;
        std::vector<IL::BlockId> successors;
        switch (terminator.kind)
        {
        case IL::TerminatorKind::Br:
            if (terminator.target < m_blockCount)
                successors.push_back(terminator.target);
            break;
        case IL::TerminatorKind::CondBr:
            if (terminator.trueTarget < m_blockCount)
                successors.push_back(terminator.trueTarget);
            if (terminator.falseTarget < m_blockCount)
                successors.push_back(terminator.falseTarget);
            break;
        case IL::TerminatorKind::Ret:
        case IL::TerminatorKind::Unreachable:
            break;
        default:
            break;
        }
        if (successors.empty())
            successors.push_back(m_exit);
        return successors;
    }

    std::size_t m_blockCount = 0;
    std::size_t m_exit = 0;
    std::vector<std::vector<bool>> m_sets;
};

class Emitter
{
public:
    explicit Emitter(const IL::Module& module, bool preferStructured = false)
        : m_module(module)
        , m_structured(preferStructured)
    {
    }

    [[nodiscard]] Result<Emission> Run()
    {
        m_typeIds.assign(m_module.types.size(), 0);
        m_useGlsl = UsesGlsl(m_module);
        m_useTestBuffer = UsesTestBuffer(m_module);
        m_useLocal = UsesLocalMemory(m_module);

        // Validate the entry point before pre-emitting helper constants, so an
        // empty module reports its missing entry point rather than a missing
        // integer type.
        if (m_module.entryFunction == IL::InvalidFunctionId ||
            m_module.entryFunction >= m_module.functions.size())
        {
            return std::unexpected(EmitError("SR-SPV-020", "module has no valid entry function"));
        }

        if (m_useTestBuffer)
            m_bufferVar = NewId();
        if (m_useLocal)
            m_localVar = NewId();
        PrepareStageIo();
        PreparePushConstants();
        PrepareImages();
        for (const IL::Function& function : m_module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op != IL::Op::Intrinsic ||
                        instruction.subop != static_cast<std::uint16_t>(IL::IntrinsicId::SpecialReg))
                        continue;
                    if (instruction.payload == IL::NoPayload)
                        continue;
                    const auto* payload = std::get_if<IL::SpecialRegPayload>(&m_module.payloads.Get(instruction.payload));
                    if (payload == nullptr)
                        continue;
                    if (payload->id == IL::SpecialRegId::ThreadIdX || payload->id == IL::SpecialRegId::ThreadIdY ||
                        payload->id == IL::SpecialRegId::ThreadIdZ)
                        (void)SpecialVariableFor(IL::Builtin::LocalInvocationId, true);
                }
            }
        }
        EmitHeader();

        if (m_useTestBuffer)
        {
            const auto declarations = EmitTestBufferDeclarations();
            if (!declarations)
                return std::unexpected(declarations.error());
        }

        if (m_useLocal)
        {
            const auto declarations = EmitLocalDeclarations();
            if (!declarations)
                return std::unexpected(declarations.error());
        }

        for (const IL::Constant& constant : m_module.constants)
        {
            const auto status = EmitConstant(constant);
            if (!status)
                return std::unexpected(status.error());
        }

        if (UsesClz(m_module))
        {
            const auto thirtyOne = U32Constant(31);
            if (!thirtyOne)
                return std::unexpected(thirtyOne.error());
        }

        {
            const auto zero = U32Constant(0);
            if (!zero)
                return std::unexpected(zero.error());
        }

        if (UsesFloatReciprocal(m_module))
        {
            const IL::TypeId f32Type = m_module.types.FindFloat(IL::FloatFormat::F32);
            if (f32Type != IL::InvalidType)
            {
                const auto type = EmitType(f32Type);
                if (!type)
                    return std::unexpected(type.error());
                m_f32One = NewId();
                Op(OpConstant, {*type, m_f32One, 0x3F800000u});
            }
            const IL::TypeId f16Type = m_module.types.FindFloat(IL::FloatFormat::F16);
            if (f16Type != IL::InvalidType)
            {
                const auto type = EmitType(f16Type);
                if (!type)
                    return std::unexpected(type.error());
                m_f16One = NewId();
                Op(OpConstant, {*type, m_f16One, 0x3C00u});
            }
            const IL::TypeId f64Type = m_module.types.FindFloat(IL::FloatFormat::F64);
            if (f64Type != IL::InvalidType)
            {
                const auto type = EmitType(f64Type);
                if (!type)
                    return std::unexpected(type.error());
                m_f64One = NewId();
                Op(OpConstant, {*type, m_f64One, 0u, 0x3FF00000u});
            }
            if (m_f32One == 0 && m_f16One == 0 && m_f64One == 0)
                return std::unexpected(EmitError("SR-SPV-046", "reciprocal requires a float type"));
        }



        m_waveScope = 0;
        if (UsesIntrinsic(m_module, IL::IntrinsicId::WaveBallot) ||
            UsesIntrinsic(m_module, IL::IntrinsicId::WaveBroadcastFirst))
        {
            const auto scope = U32Constant(3);
            if (!scope)
                return std::unexpected(scope.error());
            m_waveScope = *scope;
        }

        if (m_useDeviceMemory)
        {
            const IL::TypeId u32Type = m_module.types.FindInt(32, false);
            if (u32Type == IL::InvalidType)
                return std::unexpected(EmitError("SR-SPV-072", "device memory requires a u32 type"));
            const auto u32 = EmitType(u32Type);
            if (!u32)
                return std::unexpected(u32.error());
            m_devicePtrU32 = NewId();
            Op(OpTypePointer, {m_devicePtrU32, 5349 /* PhysicalStorageBuffer */, *u32});
        }

        if (UsesIntrinsic(m_module, IL::IntrinsicId::WaveBallot))
        {
            const IL::TypeId u32Type = m_module.types.FindInt(32, false);
            if (u32Type == IL::InvalidType)
                return std::unexpected(EmitError("SR-SPV-060", "ballot requires a u32 type"));
            const auto u32 = EmitType(u32Type);
            if (!u32)
                return std::unexpected(u32.error());

            m_ballotVecType = NewId();
            Op(OpTypeVector, {m_ballotVecType, *u32, 4});
            m_ballotPairType = NewId();
            Op(OpTypeVector, {m_ballotPairType, *u32, 2});
        }

        {
            const auto declarations = EmitImageDeclarations();
            if (!declarations)
                return std::unexpected(declarations.error());
        }

        {
            const auto declarations = EmitPushDeclarations();
            if (!declarations)
                return std::unexpected(declarations.error());
        }

        {
            const auto declarations = EmitSpecialDeclarations();
            if (!declarations)
                return std::unexpected(declarations.error());
        }

        {
            const auto declarations = EmitStageIoDeclarations();
            if (!declarations)
                return std::unexpected(declarations.error());
        }

        const auto status = EmitFunction();
        if (!status)
            return std::unexpected(status.error());

        m_words[3] = m_nextId; // bound

        Emission emission;
        emission.words = m_words;
        return emission;
    }

private:
    [[nodiscard]] static bool UsesIntrinsic(const IL::Module& module, IL::IntrinsicId id)
    {
        for (const IL::Function& function : module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op == IL::Op::Intrinsic && instruction.subop == static_cast<std::uint16_t>(id))
                        return true;
                }
            }
        }

        return false;
    }

    [[nodiscard]] static bool UsesTestBuffer(const IL::Module& module)
    {
        for (const IL::Function& function : module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op == IL::Op::Intrinsic &&
                        (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::TestBufferStore) ||
                         instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::TestBufferLoad)))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] static bool UsesLocalMemory(const IL::Module& module)
    {
        for (const IL::Function& function : module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op == IL::Op::Intrinsic &&
                        (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::LocalStore) ||
                         instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::LocalLoad)))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] static bool UsesGlsl(const IL::Module& module)
    {
        for (const IL::Function& function : module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op == IL::Op::Binary)
                    {
                        const auto subop = static_cast<IL::BinaryOp>(instruction.subop);
                        if (subop == IL::BinaryOp::FMin || subop == IL::BinaryOp::FMax)
                            return true;
                    }
                    if (instruction.op == IL::Op::Ternary)
                    {
                        const auto subop = static_cast<IL::TernaryOp>(instruction.subop);
                        if (subop == IL::TernaryOp::Fma || subop == IL::TernaryOp::FMin3 ||
                            subop == IL::TernaryOp::FMax3 || subop == IL::TernaryOp::Clamp)
                            return true;
                    }
                    if (instruction.op == IL::Op::Unary)
                    {
                        const auto subop = static_cast<IL::UnaryOp>(instruction.subop);
                        if (subop == IL::UnaryOp::FAbs || subop == IL::UnaryOp::Sqrt ||
                            subop == IL::UnaryOp::Clz || subop == IL::UnaryOp::Ctz ||
                            subop == IL::UnaryOp::Rsq || subop == IL::UnaryOp::Exp2 ||
                            subop == IL::UnaryOp::Log2 || subop == IL::UnaryOp::Rcp ||
                            subop == IL::UnaryOp::Trunc || subop == IL::UnaryOp::Ceil ||
                            subop == IL::UnaryOp::RoundEven || subop == IL::UnaryOp::Floor ||
                            subop == IL::UnaryOp::Sin || subop == IL::UnaryOp::Cos ||
                            subop == IL::UnaryOp::Fract)
                            return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] static bool UsesClz(const IL::Module& module)
    {
        for (const IL::Function& function : module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op == IL::Op::Unary &&
                        static_cast<IL::UnaryOp>(instruction.subop) == IL::UnaryOp::Clz)
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] static bool UsesInt64(const IL::Module& module)
    {
        for (std::size_t index = 0; index < module.types.size(); ++index)
        {
            const IL::Type& type = module.types.Get(static_cast<IL::TypeId>(index));
            if (type.kind == IL::TypeKind::Int && type.width == 64)
                return true;
        }
        return false;
    }

    [[nodiscard]] static bool UsesFloat16(const IL::Module& module)
    {
        for (std::size_t index = 0; index < module.types.size(); ++index)
        {
            const IL::Type& type = module.types.Get(static_cast<IL::TypeId>(index));
            if (type.kind == IL::TypeKind::Float && type.floatFormat == IL::FloatFormat::F16)
                return true;
        }
        return false;
    }

    [[nodiscard]] static bool UsesInt16(const IL::Module& module)
    {
        for (std::size_t index = 0; index < module.types.size(); ++index)
        {
            const IL::Type& type = module.types.Get(static_cast<IL::TypeId>(index));
            if (type.kind == IL::TypeKind::Int && type.width == 16)
                return true;
        }
        return false;
    }

    [[nodiscard]] static bool UsesFloat64(const IL::Module& module)
    {
        for (std::size_t index = 0; index < module.types.size(); ++index)
        {
            const IL::Type& type = module.types.Get(static_cast<IL::TypeId>(index));
            if (type.kind == IL::TypeKind::Float && type.floatFormat == IL::FloatFormat::F64)
                return true;
        }
        return false;
    }

    [[nodiscard]] static bool UsesFloatReciprocal(const IL::Module& module)
    {
        for (const IL::Function& function : module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op == IL::Op::Unary &&
                        (static_cast<IL::UnaryOp>(instruction.subop) == IL::UnaryOp::Rcp ||
                         static_cast<IL::UnaryOp>(instruction.subop) == IL::UnaryOp::Rsq))
                    {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::uint32_t NewId() noexcept { return m_nextId++; }

    void Op(std::uint16_t opcode, std::initializer_list<std::uint32_t> operands)
    {
        m_words.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16) | opcode);
        m_words.insert(m_words.end(), operands.begin(), operands.end());
    }

    void Op(std::uint16_t opcode, const std::vector<std::uint32_t>& operands)
    {
        m_words.push_back((static_cast<std::uint32_t>(operands.size() + 1) << 16) | opcode);
        m_words.insert(m_words.end(), operands.begin(), operands.end());
    }

    void EmitString(std::uint16_t opcode, std::uint32_t id, std::string_view text)
    {
        std::vector<std::uint32_t> operands;
        operands.push_back(id);
        std::uint32_t word = 0;
        std::uint32_t shift = 0;
        for (const char character : text)
        {
            word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(character)) << shift;
            shift += 8;
            if (shift == 32)
            {
                operands.push_back(word);
                word = 0;
                shift = 0;
            }
        }
        operands.push_back(word);
        Op(opcode, operands);
    }

    struct StageIo
    {
        IL::Space space = IL::Space::Input;
        IL::Builtin builtin = IL::Builtin::None;
        std::uint32_t location = 0;
        std::uint32_t variable = 0;
        std::uint32_t variablePointer = 0;
        std::uint32_t elementPointer = 0;
        IL::TypeId type = IL::InvalidType;
        std::uint32_t components = 1;
        IL::Interpolation interpolation = IL::Interpolation::None;
    };

    [[nodiscard]] static std::uint32_t ExecutionModel(IL::Stage stage) noexcept
    {
        switch (stage)
        {
        case IL::Stage::Vertex:
            return 0;
        case IL::Stage::TessControl:
            return 1;
        case IL::Stage::TessEval:
            return 2;
        case IL::Stage::Geometry:
            return 3;
        case IL::Stage::Fragment:
            return 4;
        default:
            return 5; // GLCompute
        }
    }

    // SPIR-V execution modes for the geometry topologies, kept here so the IL
    // stays vendor- and binary-neutral.
    [[nodiscard]] static std::uint32_t InputTopologyMode(IL::InputTopology topology) noexcept
    {
        switch (topology)
        {
        case IL::InputTopology::Points:
            return 19; // InputPoints
        case IL::InputTopology::Lines:
            return 20; // InputLines
        case IL::InputTopology::LinesAdjacency:
            return 21; // InputLinesAdjacency
        case IL::InputTopology::TrianglesAdjacency:
            return 23; // InputTrianglesAdjacency
        case IL::InputTopology::Triangles:
        default:
            return 22; // Triangles
        }
    }

    [[nodiscard]] static std::uint32_t OutputTopologyMode(IL::OutputTopology topology) noexcept
    {
        switch (topology)
        {
        case IL::OutputTopology::Points:
            return 27; // OutputPoints
        case IL::OutputTopology::LineStrip:
            return 28; // OutputLineStrip
        case IL::OutputTopology::TriangleStrip:
        default:
            return 29; // OutputTriangleStrip
        }
    }

    [[nodiscard]] static std::uint32_t StorageClass(IL::Space space) noexcept
    {
        return space == IL::Space::Output ? 3u : 1u; // Output : Input
    }

    [[nodiscard]] static std::uint32_t BuiltinValue(IL::Builtin builtin) noexcept
    {
        switch (builtin)
        {
        case IL::Builtin::Position:
            return 0;
        case IL::Builtin::PointSize:
            return 1;
        case IL::Builtin::ClipDistance:
            return 3;
        case IL::Builtin::CullDistance:
            return 4;
        case IL::Builtin::PrimitiveId:
            return 7;
        case IL::Builtin::Layer:
            return 9;
        case IL::Builtin::ViewportIndex:
            return 10;
        case IL::Builtin::TessCoord:
            return 13;
        case IL::Builtin::FragCoord:
            return 15;
        case IL::Builtin::FrontFacing:
            return 17;
        case IL::Builtin::SampleId:
            return 18;
        case IL::Builtin::SampleMask:
            return 20;
        case IL::Builtin::FragDepth:
            return 22;
        case IL::Builtin::NumWorkgroups:
            return 24;
        case IL::Builtin::WorkgroupId:
            return 26;
        case IL::Builtin::LocalInvocationId:
            return 27;
        case IL::Builtin::GlobalInvocationId:
            return 28;
        case IL::Builtin::LocalInvocationIndex:
            return 29;
        case IL::Builtin::SubgroupSize:
            return 36;
        case IL::Builtin::SubgroupInvocationId:
            return 41;
        case IL::Builtin::VertexIndex:
            return 42;
        case IL::Builtin::InstanceIndex:
            return 43;
        default:
            return 0;
        }
    }

    [[nodiscard]] static std::uint32_t InterpolationDecoration(IL::Interpolation interpolation) noexcept
    {
        switch (interpolation)
        {
        case IL::Interpolation::Flat:
            return 14;
        case IL::Interpolation::NoPerspective:
            return 13;
        case IL::Interpolation::Centroid:
            return 16;
        case IL::Interpolation::Sample:
            return 17;
        default:
            return 0;
        }
    }

    // Compute programs read their thread id from builtins. The variables are
    // declared once and loaded per component.
    struct SpecialVariable
    {
        IL::Builtin builtin = IL::Builtin::None;
        std::uint32_t variable = 0;
        std::uint32_t elementPointer = 0;
    };

    std::uint32_t SpecialVariableFor(IL::Builtin builtin, bool allocate)
    {
        for (const SpecialVariable& entry : m_special)
        {
            if (entry.builtin == builtin)
                return entry.variable;
        }
        if (!allocate)
            return 0;

        SpecialVariable entry;
        entry.builtin = builtin;
        entry.variable = NewId();
        m_special.push_back(entry);
        return entry.variable;
    }

    [[nodiscard]] Result<void> EmitSpecialDeclarations()
    {
        if (m_special.empty())
            return {};

        const IL::TypeId u32Type = m_module.types.FindInt(32, false);
        if (u32Type == IL::InvalidType)
            return std::unexpected(EmitError("SR-SPV-064", "builtin input requires a u32 type"));
        const auto u32 = EmitType(u32Type);
        if (!u32)
            return std::unexpected(u32.error());

        const std::uint32_t vec3 = NewId();
        Op(OpTypeVector, {vec3, *u32, 3});
        const std::uint32_t pointerVec = NewId();
        Op(OpTypePointer, {pointerVec, 1 /* Input */, vec3});
        const std::uint32_t pointerScalar = NewId();
        Op(OpTypePointer, {pointerScalar, 1 /* Input */, *u32});

        for (std::uint32_t component = 0; component < 3; ++component)
        {
            const auto index = U32Constant(component);
            if (!index)
                return std::unexpected(index.error());
        }

        for (SpecialVariable& entry : m_special)
            Op(OpVariable, {pointerVec, entry.variable, 1 /* Input */});

        for (SpecialVariable& entry : m_special)
            entry.elementPointer = pointerScalar;

        return {};
    }

    [[nodiscard]] Result<std::uint32_t> SpecialComponent(const SpecialVariable& entry, std::uint32_t component)
    {
        const auto index = U32Constant(component);
        if (!index)
            return std::unexpected(index.error());

        const std::uint32_t pointer = NewId();
        Op(OpAccessChain, {entry.elementPointer, pointer, entry.variable, *index});
        return pointer;
    }

    // Signature rows become interface variables. Ids are allocated before the
    // entry point so the interface list can name them.
    void PrepareStageIo()
    {
        const auto add = [this](const IL::SignatureDecl& decl) {
            StageIo binding;
            binding.space = decl.space;
            binding.builtin = decl.builtin;
            binding.location = decl.location;
            binding.variable = NewId();
            binding.type = decl.type;
            binding.interpolation = decl.interpolation;
            if (decl.type != IL::InvalidType && m_module.types.contains(decl.type))
            {
                const IL::Type& type = m_module.types.Get(decl.type);
                binding.components = type.kind == IL::TypeKind::Vector ? type.count : 1;
            }
            m_stageIo.push_back(binding);
        };

        for (const IL::SignatureDecl& decl : m_module.inputs)
            add(decl);
        for (const IL::SignatureDecl& decl : m_module.outputs)
            add(decl);
    }

    [[nodiscard]] const StageIo* FindStageIo(IL::Space space, IL::Builtin builtin, std::uint32_t location) const noexcept
    {
        for (const StageIo& binding : m_stageIo)
        {
            if (binding.space == space && binding.builtin == builtin && binding.location == location)
                return &binding;
        }

        return nullptr;
    }

    [[nodiscard]] Result<void> EmitStageIoDeclarations()
    {
        // Types and every index constant come first: an OpVariable ends the
        // types/constants section, so nothing may be added after it.
        std::vector<std::uint32_t> pointers(m_stageIo.size(), 0);
        bool needsIndices = false;

        for (std::size_t i = 0; i < m_stageIo.size(); ++i)
        {
            StageIo& binding = m_stageIo[i];

            const auto type = EmitType(binding.type);
            if (!type)
                return std::unexpected(type.error());

            const IL::Type& declared = m_module.types.Get(binding.type);
            const IL::TypeId elementId = declared.kind == IL::TypeKind::Vector ? declared.element : binding.type;
            const auto element = EmitType(elementId);
            if (!element)
                return std::unexpected(element.error());

            binding.variablePointer = NewId();
            Op(OpTypePointer, {binding.variablePointer, StorageClass(binding.space), *type});

            if (declared.kind == IL::TypeKind::Vector)
            {
                binding.elementPointer = NewId();
                Op(OpTypePointer, {binding.elementPointer, StorageClass(binding.space), *element});
            }
            else
            {
                binding.elementPointer = binding.variablePointer;
            }

            pointers[i] = binding.variablePointer;
            needsIndices = needsIndices || binding.components > 1;
        }

        if (needsIndices)
        {
            for (std::uint32_t component = 0; component < 4; ++component)
            {
                const auto index = U32Constant(component);
                if (!index)
                    return std::unexpected(index.error());
            }
        }

        for (std::size_t i = 0; i < m_stageIo.size(); ++i)
            Op(OpVariable, {pointers[i], m_stageIo[i].variable, StorageClass(m_stageIo[i].space)});

        return {};
    }

    // The pointer to one component of a stage IO variable.
    [[nodiscard]] Result<std::uint32_t> StageIoPointer(const StageIo& binding, std::uint32_t component)
    {
        if (binding.components == 1)
            return binding.variable;

        const auto index = U32Constant(component);
        if (!index)
            return std::unexpected(index.error());

        const std::uint32_t pointer = NewId();
        Op(OpAccessChain, {binding.elementPointer, pointer, binding.variable, *index});
        return pointer;
    }

    // User data is a push constant block: the host fills it per dispatch or
    // draw, which is how a console shader receives root constants and
    // descriptor addresses.
    void PreparePushConstants()
    {
        for (const IL::Function& function : m_module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op != IL::Op::Intrinsic ||
                        instruction.subop != static_cast<std::uint16_t>(IL::IntrinsicId::UserData))
                        continue;
                    const auto* payload = instruction.payload != IL::NoPayload
                                              ? std::get_if<IL::UserDataDesc>(&m_module.payloads.Get(instruction.payload))
                                              : nullptr;
                    if (payload == nullptr)
                        continue;
                    m_pushIndices.push_back(payload->index);
                    m_pushCount = std::max(m_pushCount, payload->index + 1);
                }
            }
        }

        if (m_pushIndices.empty())
            return;

        m_pushCount = std::max(m_pushCount, 32u);
        m_usePushConstants = true;
        m_pushVar = NewId();
        m_pushArray = NewId();
        m_pushStruct = NewId();
    }

    [[nodiscard]] Result<void> EmitPushDeclarations()
    {
        if (!m_usePushConstants)
            return {};

        const IL::TypeId u32Type = m_module.types.FindInt(32, false);
        if (u32Type == IL::InvalidType)
            return std::unexpected(EmitError("SR-SPV-070", "user data requires a u32 type"));
        const auto u32 = EmitType(u32Type);
        if (!u32)
            return std::unexpected(u32.error());

        const std::uint32_t count = NewId();
        Op(OpConstant, {*u32, count, m_pushCount});
        Op(OpTypeArray, {m_pushArray, *u32, count});
        Op(OpTypeStruct, {m_pushStruct, m_pushArray});

        const std::uint32_t pointerStruct = NewId();
        Op(OpTypePointer, {pointerStruct, 9 /* PushConstant */, m_pushStruct});
        m_pushPtrU32 = NewId();
        Op(OpTypePointer, {m_pushPtrU32, 9 /* PushConstant */, *u32});

        for (std::uint32_t index = 0; index <= m_pushCount; ++index)
        {
            const auto constant = U32Constant(index);
            if (!constant)
                return std::unexpected(constant.error());
            if (index == 0)
                m_pushZero = *constant;
        }

        Op(OpVariable, {pointerStruct, m_pushVar, 9 /* PushConstant */});
        return {};
    }

    struct ImageResource
    {
        std::uint32_t set = 0;
        std::uint32_t binding = 0;
        std::uint32_t samplerBinding = 0;
        std::uint32_t variable = 0;
        std::uint32_t samplerVariable = 0;
        std::uint32_t imageType = 0;
        std::uint32_t pointerType = 0;
        std::uint32_t sampledImageType = 0;
        std::uint32_t samplerType = 0;
        std::uint32_t samplerPointerType = 0;
        bool sampled = false;
    };
    std::vector<ImageResource> m_images;
    std::uint32_t m_imageVec4 = 0;
    std::uint32_t m_imageVec2i = 0;
    std::uint32_t m_imageVec2f = 0;
    std::uint32_t m_imageZeroLod = 0;
    std::uint32_t m_imageVec4Element = 0;

    // Image accesses name a binding the host filled from the T# descriptor.
    void PrepareImages()
    {
        const auto add = [this](const IL::ImageAccessDesc& access) {
            for (const ImageResource& image : m_images)
            {
                if (image.set == access.set && image.binding == access.binding && image.sampled == access.sampled)
                    return;
            }

            ImageResource image;
            image.set = access.set;
            image.binding = access.binding;
            image.samplerBinding = access.samplerBinding;
            image.sampled = access.sampled;
            image.variable = NewId();
            if (access.sampled)
                image.samplerVariable = NewId();
            m_images.push_back(image);
        };

        for (const IL::Function& function : m_module.functions)
        {
            for (const IL::Block& block : function.blocks)
            {
                for (const IL::Instruction& instruction : block.body)
                {
                    if (instruction.op != IL::Op::Intrinsic ||
                        (instruction.subop != static_cast<std::uint16_t>(IL::IntrinsicId::ImageLoad) &&
                         instruction.subop != static_cast<std::uint16_t>(IL::IntrinsicId::ImageStore) &&
                         instruction.subop != static_cast<std::uint16_t>(IL::IntrinsicId::ImageSample)))
                        continue;
                    if (instruction.payload == IL::NoPayload)
                        continue;
                    if (const auto* access = std::get_if<IL::ImageAccessDesc>(&m_module.payloads.Get(instruction.payload)))
                        add(*access);
                }
            }
        }
    }

    [[nodiscard]] Result<void> EmitImageDeclarations()
    {
        if (m_images.empty())
            return {};

        // The module may have neither type: image texels are 32-bit regardless
        // of what the rest of the shader uses, so declare them here.
        const IL::TypeId f32Type = m_module.types.FindFloat(IL::FloatFormat::F32);
        std::uint32_t f32Id = 0;
        if (f32Type != IL::InvalidType)
        {
            const auto f32 = EmitType(f32Type);
            if (!f32)
                return std::unexpected(f32.error());
            f32Id = *f32;
        }
        else
        {
            f32Id = NewId();
            Op(OpTypeFloat, {f32Id, 32});
        }

        const IL::TypeId u32Type = m_module.types.FindInt(32, false);
        std::uint32_t u32Id = 0;
        if (u32Type != IL::InvalidType)
        {
            const auto u32 = EmitType(u32Type);
            if (!u32)
                return std::unexpected(u32.error());
            u32Id = *u32;
        }
        else
        {
            u32Id = NewId();
            Op(OpTypeInt, {u32Id, 32, 0});
        }

        const std::uint32_t* f32 = &f32Id;
        const std::uint32_t* u32 = &u32Id;

        m_imageVec4 = NewId();
        Op(OpTypeVector, {m_imageVec4, *f32, 4});
        m_imageVec4Element = f32Id;
        m_imageVec2i = NewId();
        Op(OpTypeVector, {m_imageVec2i, *u32, 2});
        m_imageVec2f = NewId();
        Op(OpTypeVector, {m_imageVec2f, f32Id, 2});

        // Compute has no derivatives, so sampling states the level explicitly:
        // level zero is a plain texel fetch through the sampler.
        m_imageZeroLod = NewId();
        Op(OpConstant, {f32Id, m_imageZeroLod, 0});

        for (ImageResource& image : m_images)
        {
            // A 2D storage image with a 32-bit float RGBA format: image_load and
            // image_store map to OpImageRead and OpImageWrite without a sampler.
            image.imageType = NewId();
            if (image.sampled)
            {
                // A sampled image: read through a sampler with normalized
                // float coordinates, format unknown until the view is bound.
                Op(OpTypeImage, {image.imageType, *f32, 1 /* 2D */, 0 /* depth */, 0 /* arrayed */, 0 /* ms */,
                                 1 /* sampled */, 0 /* unknown format */});
                image.sampledImageType = NewId();
                Op(OpTypeSampledImage, {image.sampledImageType, image.imageType});
                image.samplerType = NewId();
                Op(OpTypeSampler, {image.samplerType});
            }
            else
            {
                // Sampled = 2 is Vulkan's storage image: read and written
                // through OpImageRead/OpImageWrite without a sampler.
                Op(OpTypeImage, {image.imageType, *f32, 1 /* 2D */, 0 /* depth */, 0 /* arrayed */, 0 /* ms */,
                                 2 /* sampled */, 1 /* Rgba32f */});
            }

            image.pointerType = NewId();
            Op(OpTypePointer, {image.pointerType, 0 /* UniformConstant */, image.imageType});
            if (image.sampled)
            {
                image.samplerPointerType = NewId();
                Op(OpTypePointer, {image.samplerPointerType, 0 /* UniformConstant */, image.samplerType});
            }
        }

        for (const ImageResource& image : m_images)
        {
            Op(OpVariable, {image.pointerType, image.variable, 0 /* UniformConstant */});
            if (image.sampled)
                Op(OpVariable, {image.samplerPointerType, image.samplerVariable, 0 /* UniformConstant */});
        }

        return {};
    }

    [[nodiscard]] const ImageResource* FindImage(std::uint32_t set, std::uint32_t binding) const noexcept
    {
        for (const ImageResource& image : m_images)
        {
            if (image.set == set && image.binding == binding)
                return &image;
        }
        return nullptr;
    }

    void EmitHeader()
    {
        m_words.push_back(kMagic);
        m_words.push_back(kVersion);
        m_words.push_back(0); // generator
        m_words.push_back(0); // bound, patched at the end
        m_words.push_back(0); // schema

        Op(OpCapability, {1}); // Shader
        if (UsesInt64(m_module))
            Op(OpCapability, {11}); // Int64
        if (UsesFloat16(m_module))
            Op(OpCapability, {9}); // Float16
        if (UsesInt16(m_module))
            Op(OpCapability, {22}); // Int16
        if (UsesFloat64(m_module))
            Op(OpCapability, {10}); // Float64
        if (UsesIntrinsic(m_module, IL::IntrinsicId::WaveBallot) ||
            UsesIntrinsic(m_module, IL::IntrinsicId::WaveBroadcastFirst))
        {
            Op(OpCapability, {61}); // GroupNonUniform
            Op(OpCapability, {64}); // GroupNonUniformBallot (BroadcastFirst needs it too)
        }
        if (m_module.stage == IL::Stage::Geometry)
            Op(OpCapability, {2}); // Geometry
        if (UsesIntrinsic(m_module, IL::IntrinsicId::DeviceLoad) ||
            UsesIntrinsic(m_module, IL::IntrinsicId::DeviceStore))
        {
            Op(OpCapability, {5347}); // PhysicalStorageBufferAddresses
            m_useDeviceMemory = true;
        }
        if (m_useGlsl)
        {
            m_glslSet = NewId();
            EmitString(OpExtInstImport, m_glslSet, "GLSL.std.450");
        }
        Op(OpMemoryModel, {m_useDeviceMemory ? 5348u : 0u, 1}); // PhysicalStorageBuffer64 or Logical, GLSL450

        m_mainId = NewId();
        const std::uint32_t executionModel = ExecutionModel(m_module.stage);

        std::vector<std::uint32_t> operands;
        operands.push_back(executionModel);
        operands.push_back(m_mainId);
        const std::string text = m_module.entryPoint.empty() ? "main" : m_module.entryPoint;
        std::uint32_t word = 0;
        std::uint32_t shift = 0;
        for (const char character : text)
        {
            word |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(character)) << shift;
            shift += 8;
            if (shift == 32)
            {
                operands.push_back(word);
                word = 0;
                shift = 0;
            }
        }
        operands.push_back(word);
        if (m_useTestBuffer)
            operands.push_back(m_bufferVar);
        if (m_useLocal)
            operands.push_back(m_localVar);
        for (const StageIo& binding : m_stageIo)
            operands.push_back(binding.variable);
        for (const SpecialVariable& entry : m_special)
            operands.push_back(entry.variable);
        if (m_usePushConstants)
            operands.push_back(m_pushVar);
        for (const ImageResource& image : m_images)
        {
            operands.push_back(image.variable);
            if (image.sampled)
                operands.push_back(image.samplerVariable);
        }
        Op(OpEntryPoint, operands);

        if (executionModel == 5) // GLCompute
        {
            Op(OpExecutionMode,
               {m_mainId,
                17, // LocalSize
                m_module.executionModes.workgroupSize[0],
                m_module.executionModes.workgroupSize[1],
                m_module.executionModes.workgroupSize[2]});
        }
        else if (executionModel == 3) // Geometry
        {
            Op(OpExecutionMode, {m_mainId, InputTopologyMode(m_module.executionModes.inputTopology)});
            Op(OpExecutionMode, {m_mainId, OutputTopologyMode(m_module.executionModes.outputTopology)});
            Op(OpExecutionMode,
               {m_mainId, 26 /* OutputVertices */, m_module.executionModes.outputVertices});
        }
        else if (executionModel == 4) // Fragment
        {
            Op(OpExecutionMode, {m_mainId, 7}); // OriginUpperLeft
            for (const IL::SignatureDecl& output : m_module.outputs)
            {
                if (output.builtin == IL::Builtin::FragDepth)
                {
                    Op(OpExecutionMode, {m_mainId, 12}); // DepthReplacing
                    break;
                }
            }
        }

        for (const SpecialVariable& entry : m_special)
            Op(OpDecorate, {entry.variable, 11 /* Builtin */, BuiltinValue(entry.builtin)});

        for (const ImageResource& image : m_images)
        {
            Op(OpDecorate, {image.variable, 34 /* DescriptorSet */, image.set});
            Op(OpDecorate, {image.variable, 33 /* Binding */, image.binding});
            if (image.sampled)
            {
                Op(OpDecorate, {image.samplerVariable, 34 /* DescriptorSet */, image.set});
                Op(OpDecorate, {image.samplerVariable, 33 /* Binding */, image.samplerBinding});
            }
        }

        if (m_usePushConstants)
        {
            Op(OpDecorate, {m_pushStruct, 2 /* Block */});
            Op(OpMemberDecorate, {m_pushStruct, 0, 35 /* Offset */, 0});
            Op(OpDecorate, {m_pushArray, 6 /* ArrayStride */, 4});
        }

        // Annotations for the stage interface precede every type declaration.
        for (const StageIo& binding : m_stageIo)
        {
            if (binding.builtin != IL::Builtin::None)
            {
                Op(OpDecorate, {binding.variable, 11 /* Builtin */, BuiltinValue(binding.builtin)});
            }
            else
            {
                Op(OpDecorate, {binding.variable, 30 /* Location */, binding.location});
                const std::uint32_t interpolation = InterpolationDecoration(binding.interpolation);
                if (interpolation != 0)
                    Op(OpDecorate, {binding.variable, interpolation});
            }
        }
    }

    [[nodiscard]] Result<void> EmitTestBufferDeclarations()
    {
        const IL::TypeId u32Type = m_module.types.FindInt(32, false);
        if (u32Type == IL::InvalidType)
            return std::unexpected(EmitError("SR-SPV-007", "module has no u32 type"));

        std::uint32_t u32 = m_typeIds[u32Type];
        const bool emitU32 = u32 == 0;
        if (emitU32)
        {
            u32 = NewId();
            m_typeIds[u32Type] = u32;
        }

        const std::uint32_t runtimeArray = NewId();
        const std::uint32_t bufferStruct = NewId();
        const std::uint32_t pointerStruct = NewId();
        const std::uint32_t pointerU32 = NewId();

        // Annotations precede every type declaration.
        Op(OpDecorate, {m_bufferVar, 34 /* DescriptorSet */, 0});
        Op(OpDecorate, {m_bufferVar, 33 /* Binding */, 0});
        Op(OpDecorate, {bufferStruct, 2 /* Block */});
        Op(OpMemberDecorate, {bufferStruct, 0, 35 /* Offset */, 0});
        Op(OpDecorate, {runtimeArray, 6 /* ArrayStride */, 4});

        if (emitU32)
            Op(OpTypeInt, {u32, 32, 0});

        Op(OpTypeRuntimeArray, {runtimeArray, u32});
        Op(OpTypeStruct, {bufferStruct, runtimeArray});
        Op(OpTypePointer, {pointerStruct, 12 /* StorageBuffer */, bufferStruct});
        Op(OpTypePointer, {pointerU32, 12 /* StorageBuffer */, u32});

        const auto zero = U32Constant(0);
        if (!zero)
            return std::unexpected(zero.error());

        Op(OpVariable, {pointerStruct, m_bufferVar, 12 /* StorageBuffer */});

        m_bufferPtrU32 = pointerU32;
        m_bufferZero = *zero;
        return {};
    }

    [[nodiscard]] Result<void> EmitLocalDeclarations()
    {
        const IL::TypeId u32Type = m_module.types.FindInt(32, false);
        if (u32Type == IL::InvalidType)
            return std::unexpected(EmitError("SR-SPV-007", "module has no u32 type"));

        std::uint32_t u32 = m_typeIds[u32Type];
        const bool emitU32 = u32 == 0;
        if (emitU32)
        {
            u32 = NewId();
            m_typeIds[u32Type] = u32;
        }

        // Workgroup-scope scratch for LDS: 64 KiB of dwords, addressed by byte
        // offset / 4. Real shaders use less, and the size stays within the
        // minimum Vulkan limit for compute.
        constexpr std::uint32_t dwordCount = 16384;

        const std::uint32_t countId = NewId();
        const std::uint32_t arrayType = NewId();
        const std::uint32_t pointerArray = NewId();
        const std::uint32_t pointerU32 = NewId();

        if (emitU32)
            Op(OpTypeInt, {u32, 32, 0});

        Op(OpConstant, {u32, countId, dwordCount});
        Op(OpTypeArray, {arrayType, u32, countId});
        Op(OpTypePointer, {pointerArray, 4 /* Workgroup */, arrayType});
        Op(OpTypePointer, {pointerU32, 4 /* Workgroup */, u32});
        Op(OpVariable, {pointerArray, m_localVar, 4 /* Workgroup */});

        const auto zero = U32Constant(0);
        if (!zero)
            return std::unexpected(zero.error());

        m_localPtrU32 = pointerU32;
        m_localZero = *zero;
        return {};
    }

    [[nodiscard]] Result<std::uint32_t> EmitType(IL::TypeId typeId)    {
        if (typeId == IL::InvalidType || !m_module.types.contains(typeId))
            return std::unexpected(EmitError("SR-SPV-001", "invalid type id"));
        if (m_typeIds[typeId] != 0)
            return m_typeIds[typeId];

        const IL::Type& type = m_module.types.Get(typeId);
        const std::uint32_t id = NewId();
        m_typeIds[typeId] = id;

        switch (type.kind)
        {
        case IL::TypeKind::Void:
            Op(OpTypeVoid, {id});
            break;
        case IL::TypeKind::Bool:
            Op(OpTypeBool, {id});
            break;
        case IL::TypeKind::Int:
            Op(OpTypeInt, {id, type.width, type.isSigned ? 1u : 0u});
            break;
        case IL::TypeKind::Float:
        {
            std::uint32_t width = 32;
            switch (type.floatFormat)
            {
            case IL::FloatFormat::F16:
                width = 16;
                break;
            case IL::FloatFormat::F32:
                width = 32;
                break;
            case IL::FloatFormat::F64:
                width = 64;
                break;
            default:
                return std::unexpected(EmitError("SR-SPV-002", "unsupported float format for SPIR-V emission"));
            }
            Op(OpTypeFloat, {id, width});
            break;
        }
        case IL::TypeKind::Vector:
        {
            const auto element = EmitType(type.element);
            if (!element)
                return std::unexpected(element.error());
            Op(OpTypeVector, {id, *element, type.count});
            break;
        }
        case IL::TypeKind::Pointer:
        {
            if (type.space != IL::Space::Private)
                return std::unexpected(EmitError("SR-SPV-003", "only function-local pointers are supported"));
            const auto pointee = EmitType(type.element);
            if (!pointee)
                return std::unexpected(pointee.error());
            Op(OpTypePointer, {id, 7 /* Function */, *pointee}); // Function storage
            break;
        }
        default:
            return std::unexpected(EmitError("SR-SPV-004",
                                             "unsupported type in SPIR-V emission: " +
                                                 std::string(IL::ToString(type.kind))));
        }

        return id;
    }

    [[nodiscard]] Result<void> EmitConstant(const IL::Constant& constant)
    {
        const auto type = EmitType(constant.type);
        if (!type)
            return std::unexpected(type.error());

        const std::uint32_t id = NewId();
        const IL::Type& typeValue = m_module.types.Get(constant.type);

        if (typeValue.kind == IL::TypeKind::Bool)
        {
            Op(constant.bits != 0 ? OpConstantTrue : OpConstantFalse, {*type, id});
        }
        else if (typeValue.kind == IL::TypeKind::Int || typeValue.kind == IL::TypeKind::Float)
        {
            std::vector<std::uint32_t> operands = {*type, id};
            operands.push_back(static_cast<std::uint32_t>(constant.bits));
            if ((typeValue.kind == IL::TypeKind::Int && typeValue.width == 64) ||
                (typeValue.kind == IL::TypeKind::Float && typeValue.floatFormat == IL::FloatFormat::F64))
            {
                operands.push_back(static_cast<std::uint32_t>(constant.bits >> 32));
            }
            Op(OpConstant, operands);
        }
        else
        {
            return std::unexpected(EmitError("SR-SPV-005", "unsupported constant type"));
        }

        m_constantIds.emplace(Key(constant.type, constant.bits), id);
        return {};
    }

    [[nodiscard]] static std::uint64_t Key(IL::TypeId type, std::uint64_t bits) noexcept
    {
        return (static_cast<std::uint64_t>(type) << 32) ^ (bits & 0xFFFFFFFFu);
    }

    [[nodiscard]] Result<std::uint32_t> U32Constant(std::uint32_t value)
    {
        const auto it = m_u32Constants.find(value);
        if (it != m_u32Constants.end())
            return it->second;

        const IL::TypeId u32Type = m_module.types.FindInt(32, false);
        if (u32Type == IL::InvalidType)
            return std::unexpected(EmitError("SR-SPV-006", "module has no u32 type"));
        const auto type = EmitType(u32Type);
        if (!type)
            return std::unexpected(type.error());

        const std::uint32_t id = NewId();
        Op(OpConstant, {*type, id, value});
        m_u32Constants.emplace(value, id);
        return id;
    }

    [[nodiscard]] Result<std::uint32_t> ValueId(const IL::Function& function, IL::ValueId value)
    {
        if (value >= function.values.size())
            return std::unexpected(EmitError("SR-SPV-010", "value id out of range"));

        if (m_valueIds[value] != 0)
            return m_valueIds[value];

        if (value < m_spilled.size() && m_spilled[value] && m_currentBlock != IL::InvalidBlockId)
        {
            if (m_isPhi[value] || function.values[value].block != m_currentBlock)
            {
                auto& loads = m_blockLoads[m_currentBlock];
                const auto it = loads.find(value);
                if (it != loads.end())
                    return it->second;

                const auto type = EmitType(function.values[value].type);
                if (!type)
                    return std::unexpected(type.error());
                const std::uint32_t id = NewId();
                Op(OpLoad, {*type, id, m_spillVars[value]});
                loads.emplace(value, id);
                return id;
            }
        }

        const IL::Value& definition = function.values[value];
        switch (definition.kind)
        {
        case IL::ValueKind::Constant:
        {
            const IL::Constant& constant = m_module.constants[definition.index];
            const auto it = m_constantIds.find(Key(constant.type, constant.bits));
            if (it == m_constantIds.end())
                return std::unexpected(EmitError("SR-SPV-011", "constant was not emitted"));
            m_valueIds[value] = it->second;
            return it->second;
        }
        case IL::ValueKind::Undef:
        {
            const auto type = EmitType(definition.type);
            if (!type)
                return std::unexpected(type.error());
            const std::uint32_t id = NewId();
            Op(OpUndef, {*type, id});
            m_valueIds[value] = id;
            return id;
        }
        default:
            return std::unexpected(EmitError("SR-SPV-012", "value has no SPIR-V definition"));
        }
    }

    [[nodiscard]] Result<void> EmitFunction()
    {
        if (m_module.entryFunction == IL::InvalidFunctionId || m_module.entryFunction >= m_module.functions.size())
            return std::unexpected(EmitError("SR-SPV-020", "module has no valid entry function"));

        const IL::Function& function = m_module.functions[m_module.entryFunction];
        if (function.blocks.empty() || function.entry == IL::InvalidBlockId)
            return std::unexpected(EmitError("SR-SPV-021", "function has no blocks"));

        const auto returnType = EmitType(function.returnType);
        if (!returnType)
            return std::unexpected(returnType.error());

        for (const IL::Value& value : function.values)
        {
            const auto type = EmitType(value.type);
            if (!type)
                return std::unexpected(type.error());
        }

        const bool multiBlock = function.blocks.size() > 1;
        bool structured = false;
        if (multiBlock && m_structured)
            structured = PrepareStructure(function);

        if (multiBlock && !structured)
        {
            const auto status = PrepareSsa(function);
            if (!status)
                return std::unexpected(status.error());
        }

        // Constants for dispatcher state must exist in the global section.
        if (multiBlock && !structured)
        {
            for (IL::BlockId block = 0; block < function.blocks.size(); ++block)
            {
                const auto constant = U32Constant(block);
                if (!constant)
                    return std::unexpected(constant.error());
            }
        }

        const std::uint32_t functionType = NewId();
        Op(OpTypeFunction, {functionType, *returnType});

        if (multiBlock && !structured)
        {
            const IL::TypeId u32Type = m_module.types.FindInt(32, false);
            if (u32Type == IL::InvalidType)
                return std::unexpected(EmitError("SR-SPV-022", "module has no u32 type"));
            const auto u32 = EmitType(u32Type);
            if (!u32)
                return std::unexpected(u32.error());
        }

        Op(OpFunction, {*returnType, m_mainId, 0, functionType});

        if (structured)
        {
            const auto status = EmitStructured(function);
            if (!status)
                return std::unexpected(status.error());
        }
        else if (multiBlock)
        {
            m_valueIds.assign(function.values.size(), 0);
            const auto status = EmitDispatcher(function);
            if (!status)
                return std::unexpected(status.error());
        }
        else
        {
            m_valueIds.assign(function.values.size(), 0);
            const auto status = EmitStraightLine(function);
            if (!status)
                return std::unexpected(status.error());
        }

        Op(OpFunctionEnd, {});
        return {};
    }

    // Prepares a structured layout for reducible control flow: while-style
    // natural loops and acyclic conditionals with a real post-dominator merge.
    // Anything outside that shape falls back to the dispatcher.
    [[nodiscard]] bool PrepareStructure(const IL::Function& function)
    {
        const std::size_t count = function.blocks.size();
        m_analysis = IL::AnalyzeFunction(function);
        m_post.emplace(function);
        m_loopMerge.assign(count, IL::InvalidBlockId);
        m_loopContinue.assign(count, IL::InvalidBlockId);
        m_membership.assign(count, std::vector<bool>(count, false));

        for (const IL::NaturalLoop& loop : m_analysis.loops)
        {
            if (loop.header >= count || loop.blocks.empty())
                return false;

            std::vector<bool> member(count, false);
            for (const IL::BlockId block : loop.blocks)
            {
                if (block >= count)
                    return false;
                member[block] = true;
            }

            IL::BlockId latch = IL::InvalidBlockId;
            for (const IL::BlockId block : loop.blocks)
            {
                for (const IL::BlockId successor : IL::SuccessorsOf(function.blocks[block].terminator))
                {
                    if (successor != loop.header)
                        continue;
                    if (latch != IL::InvalidBlockId && latch != block)
                        return false;
                    latch = block;
                }
            }
            if (latch == IL::InvalidBlockId)
                return false;

            IL::BlockId exit = IL::InvalidBlockId;
            for (const IL::BlockId block : loop.blocks)
            {
                for (const IL::BlockId successor : IL::SuccessorsOf(function.blocks[block].terminator))
                {
                    if (successor >= count || member[successor])
                        continue;
                    if (exit != IL::InvalidBlockId && exit != successor)
                        return false;
                    exit = successor;
                }
            }
            if (exit == IL::InvalidBlockId)
                return false;

            // Only while-style headers (a conditional at the header) are
            // structured; do-while latches fall back to the dispatcher.
            const IL::Terminator& header = function.blocks[loop.header].terminator;
            if (header.kind != IL::TerminatorKind::CondBr)
                return false;

            const IL::BlockId first = header.trueTarget;
            const IL::BlockId second = header.falseTarget;
            IL::BlockId body = IL::InvalidBlockId;
            if (first == exit && second != exit)
                body = second;
            else if (second == exit && first != exit)
                body = first;
            else
                return false;
            if (body >= count || !member[body])
                return false;

            m_loopMerge[loop.header] = exit;
            m_loopContinue[loop.header] = latch;
            m_membership[loop.header] = std::move(member);
        }

        std::vector<bool> insideLoop(count, false);
        for (const IL::NaturalLoop& loop : m_analysis.loops)
        {
            for (const IL::BlockId block : loop.blocks)
                insideLoop[block] = true;
        }

        for (const IL::BlockId block : m_analysis.rpo)
        {
            const IL::Terminator& terminator = function.blocks[block].terminator;
            if (terminator.kind == IL::TerminatorKind::Switch ||
                terminator.kind == IL::TerminatorKind::IndirectBr)
            {
                return false;
            }

            if (terminator.kind != IL::TerminatorKind::CondBr)
                continue;
            if (m_loopMerge[block] != IL::InvalidBlockId)
                continue;
            if (insideLoop[block])
                return false; // conditionals inside loop bodies are not structured yet

            const IL::BlockId merge = m_post->Immediate(block);
            if (merge == IL::InvalidBlockId || merge == m_post->Exit())
                return false;
        }

        return true;
    }

    [[nodiscard]] std::uint32_t Label(IL::BlockId block) const noexcept
    {
        return block < m_blockLabels.size() ? m_blockLabels[block] : 0;
    }

    [[nodiscard]] Result<void> EmitStructured(const IL::Function& function)
    {
        const std::size_t count = function.blocks.size();
        m_blockLabels.assign(count, 0);
        m_emitted.assign(count, false);
        for (std::uint32_t& label : m_blockLabels)
            label = NewId();

        m_valueIds.assign(function.values.size(), 0);
        for (IL::ValueId value = 0; value < function.values.size(); ++value)
        {
            if (function.values[value].kind == IL::ValueKind::Instruction)
                m_valueIds[value] = NewId();
        }

        const std::uint32_t setup = NewId();
        Op(OpLabel, {setup});

        const auto allocas = EmitAllocas(function);
        if (!allocas)
            return std::unexpected(allocas.error());

        Op(OpBranch, {m_blockLabels[function.entry]});
        return EmitStructuredBlock(function, function.entry, IL::InvalidBlockId);
    }

    [[nodiscard]] Result<void> EmitStructuredBlock(const IL::Function& function,
                                                   IL::BlockId block,
                                                   IL::BlockId stop)
    {
        if (block == IL::InvalidBlockId || block == stop)
            return {};
        if (block >= function.blocks.size())
            return std::unexpected(EmitError("SR-SPV-034", "structured branch target is out of range"));

        if (m_emitted[block])
        {
            // A block reached twice outside a proper merge means the CFG did
            // not match the supported shape. Emit the branch so validation can
            // reject this attempt and the dispatcher fallback takes over.
            Op(OpBranch, {m_blockLabels[block]});
            return {};
        }

        m_emitted[block] = true;
        Op(OpLabel, {m_blockLabels[block]});

        const auto phis = EmitPhis(function, block);
        if (!phis)
            return std::unexpected(phis.error());

        const IL::Block& body = function.blocks[block];
        for (const IL::Instruction& instruction : body.body)
        {
            if (instruction.op == IL::Op::Alloca || instruction.op == IL::Op::Phi)
                continue;
            const auto status = EmitInstruction(function, instruction);
            if (!status)
                return std::unexpected(status.error());
        }

        const IL::Terminator& terminator = body.terminator;
        switch (terminator.kind)
        {
        case IL::TerminatorKind::Br:
        {
            if (terminator.target == stop)
            {
                Op(OpBranch, {m_blockLabels[stop]});
                return {};
            }
            Op(OpBranch, {m_blockLabels[terminator.target]});
            if (m_emitted[terminator.target])
                return {};
            return EmitStructuredBlock(function, terminator.target, stop);
        }
        case IL::TerminatorKind::CondBr:
        {
            const auto condition = ValueId(function, terminator.condition);
            if (!condition)
                return std::unexpected(condition.error());

            if (m_loopMerge[block] != IL::InvalidBlockId)
            {
                const IL::BlockId merge = m_loopMerge[block];
                const IL::BlockId continueTarget = m_loopContinue[block];
                Op(OpLoopMerge, {m_blockLabels[merge], m_blockLabels[continueTarget], 0});
                Op(OpBranchConditional,
                   {*condition, m_blockLabels[terminator.trueTarget], m_blockLabels[terminator.falseTarget]});

                const IL::BlockId bodyTarget = terminator.trueTarget == merge ? terminator.falseTarget
                                                                              : terminator.trueTarget;
                if (bodyTarget == merge)
                    return std::unexpected(EmitError("SR-SPV-035", "loop has no body target"));

                const auto status = EmitStructuredBlock(function, bodyTarget, merge);
                if (!status)
                    return std::unexpected(status.error());
                if (merge == stop)
                    return {};
                return EmitStructuredBlock(function, merge, stop);            }

            const IL::BlockId merge = m_post ? m_post->Immediate(block) : IL::InvalidBlockId;
            if (merge == IL::InvalidBlockId || merge == m_post->Exit())
                return std::unexpected(EmitError("SR-SPV-036", "conditional block has no structured merge"));

            Op(OpSelectionMerge, {m_blockLabels[merge], 0});
            Op(OpBranchConditional,
               {*condition, m_blockLabels[terminator.trueTarget], m_blockLabels[terminator.falseTarget]});

            if (terminator.trueTarget != merge && !m_emitted[terminator.trueTarget])
            {
                const auto status = EmitStructuredBlock(function, terminator.trueTarget, merge);
                if (!status)
                    return std::unexpected(status.error());
            }
            if (terminator.falseTarget != merge && !m_emitted[terminator.falseTarget])
            {
                const auto status = EmitStructuredBlock(function, terminator.falseTarget, merge);
                if (!status)
                    return std::unexpected(status.error());
            }
            if (merge == stop)
                return {};
            return EmitStructuredBlock(function, merge, stop);
        }
        case IL::TerminatorKind::Ret:
            Op(OpReturn, {});
            return {};
        case IL::TerminatorKind::Unreachable:
            Op(OpUnreachable, {});
            return {};
        default:
            return std::unexpected(EmitError("SR-SPV-037", "unsupported structured terminator", terminator.sourcePc));
        }
    }

    [[nodiscard]] Result<void> EmitPhis(const IL::Function& function, IL::BlockId block)
    {
        for (const IL::Instruction& instruction : function.blocks[block].body)
        {
            if (instruction.op != IL::Op::Phi)
                continue;
            if (instruction.results.size() != 1)
                return std::unexpected(EmitError("SR-SPV-038", "phi must have one result", instruction.sourcePc));

            const IL::ValueId resultValue = instruction.results[0];
            const auto type = EmitType(function.values[resultValue].type);
            if (!type)
                return std::unexpected(type.error());

            const std::uint32_t resultId = m_valueIds[resultValue] != 0 ? m_valueIds[resultValue] : NewId();
            std::vector<std::uint32_t> operands = {*type, resultId};
            for (const IL::PhiIncoming& incoming : instruction.incoming)
            {
                const auto value = ValueId(function, incoming.value);
                if (!value)
                    return std::unexpected(value.error());
                if (incoming.block >= m_blockLabels.size())
                    return std::unexpected(EmitError("SR-SPV-039", "phi predecessor is out of range", instruction.sourcePc));
                operands.push_back(*value);
                operands.push_back(m_blockLabels[incoming.block]);
            }
            Op(OpPhi, operands);
            m_valueIds[resultValue] = resultId;
        }
        return {};
    }

    [[nodiscard]] Result<std::uint32_t> FunctionPointerType(IL::TypeId valueType)
    {
        const auto it = m_functionPointerTypes.find(valueType);
        if (it != m_functionPointerTypes.end())
            return it->second;

        const auto pointee = EmitType(valueType);
        if (!pointee)
            return std::unexpected(pointee.error());

        const std::uint32_t id = NewId();
        Op(OpTypePointer, {id, 7 /* Function */, *pointee});
        m_functionPointerTypes.emplace(valueType, id);
        return id;
    }

    [[nodiscard]] Result<void> PrepareSsa(const IL::Function& function)
    {
        const std::size_t valueCount = function.values.size();
        m_spilled.assign(valueCount, false);
        m_isPhi.assign(valueCount, false);
        m_spillVars.assign(valueCount, 0);
        m_blockUses.assign(function.blocks.size(), {});
        m_blockLoads.assign(function.blocks.size(), {});

        const auto markUse = [&](IL::BlockId block, IL::ValueId value) {
            if (value < valueCount && block < m_blockUses.size())
                m_blockUses[block].push_back(value);
        };

        for (IL::BlockId block = 0; block < function.blocks.size(); ++block)
        {
            const IL::Block& body = function.blocks[block];
            for (const IL::Instruction& instruction : body.body)
            {
                if (instruction.op == IL::Op::Phi && !instruction.results.empty())
                {
                    m_spilled[instruction.results[0]] = true;
                    m_isPhi[instruction.results[0]] = true;
                }

                for (const IL::ValueId operand : instruction.operands)
                    markUse(block, operand);
                for (const auto& incoming : instruction.incoming)
                    markUse(incoming.block, incoming.value);
            }

            if (body.terminator.condition != IL::InvalidValue)
                markUse(block, body.terminator.condition);
            if (body.terminator.selector != IL::InvalidValue)
                markUse(block, body.terminator.selector);
            if (body.terminator.hasReturnValue)
                markUse(block, body.terminator.returnValue);
        }

        for (IL::BlockId block = 0; block < function.blocks.size(); ++block)
        {
            for (const IL::ValueId value : m_blockUses[block])
            {
                if (value >= valueCount)
                    continue;
                const IL::Value& definition = function.values[value];
                if (definition.kind != IL::ValueKind::Instruction)
                    continue;
                if (m_isPhi[value] || definition.block != block)
                    m_spilled[value] = true;
            }
        }

        for (IL::ValueId value = 0; value < valueCount; ++value)
        {
            if (!m_spilled[value])
                continue;

            const auto pointer = FunctionPointerType(function.values[value].type);
            if (!pointer)
                return std::unexpected(pointer.error());
            m_spillVars[value] = NewId();
        }

        return {};
    }

    [[nodiscard]] Result<void> EmitSpillVariables(const IL::Function& function)
    {
        for (IL::ValueId value = 0; value < m_spilled.size(); ++value)
        {
            if (!m_spilled[value])
                continue;

            const auto pointer = FunctionPointerType(function.values[value].type);
            if (!pointer)
                return std::unexpected(pointer.error());
            Op(OpVariable, {*pointer, m_spillVars[value], 7 /* Function */});
        }

        return {};
    }

    [[nodiscard]] Result<void> LoadSpilledForBlock(const IL::Function& function, IL::BlockId block)
    {
        m_currentBlock = block;
        auto& loads = m_blockLoads[block];
        loads.clear();

        for (const IL::ValueId value : m_blockUses[block])
        {
            if (value >= m_spilled.size() || !m_spilled[value])
                continue;
            if (!m_isPhi[value] && function.values[value].block == block)
                continue;
            if (loads.contains(value))
                continue;

            const auto type = EmitType(function.values[value].type);
            if (!type)
                return std::unexpected(type.error());
            const std::uint32_t id = NewId();
            Op(OpLoad, {*type, id, m_spillVars[value]});
            loads.emplace(value, id);
        }

        return {};
    }

    [[nodiscard]] Result<void> StorePhiIncoming(const IL::Function& function, IL::BlockId source, IL::BlockId target)
    {
        for (const IL::Instruction& instruction : function.blocks[target].body)
        {
            if (instruction.op != IL::Op::Phi || instruction.results.empty())
                continue;

            for (const auto& incoming : instruction.incoming)
            {
                if (incoming.block != source)
                    continue;
                const auto value = ValueId(function, incoming.value);
                if (!value)
                    return std::unexpected(value.error());
                Op(OpStore, {m_spillVars[instruction.results[0]], *value});
            }
        }

        return {};
    }

    [[nodiscard]] Result<void> EmitAllocas(const IL::Function& function)
    {
        for (const IL::Instruction& instruction : function.blocks[function.entry].body)
        {
            if (instruction.op != IL::Op::Alloca)
                continue;

            const IL::ValueId result = instruction.results[0];
            const auto type = EmitType(function.values[result].type);
            if (!type)
                return std::unexpected(type.error());

            const std::uint32_t id =
                (result < m_valueIds.size() && m_valueIds[result] != 0) ? m_valueIds[result] : NewId();
            Op(OpVariable, {*type, id, 7 /* Function */});
            m_valueIds[result] = id;

            // Zero-initialise 32-bit register slots. The hardware starts with
            // undefined registers; the test ABI needs a deterministic value so
            // a shader that reads a slot before writing it behaves the same on
            // the interpreter and the device.
            const IL::Type& pointerType = m_module.types.Get(function.values[result].type);
            if (pointerType.kind == IL::TypeKind::Pointer && pointerType.element != IL::InvalidType)
            {
                const IL::Type& pointee = m_module.types.Get(pointerType.element);
                if (pointee.kind == IL::TypeKind::Int && pointee.width == 32)
                {
                    const auto zero = m_u32Constants.find(0);
                    if (zero != m_u32Constants.end())
                        Op(OpStore, {id, zero->second});
                }
            }
        }

        return {};
    }

    [[nodiscard]] Result<void> EmitStraightLine(const IL::Function& function)
    {
        const std::uint32_t label = NewId();
        Op(OpLabel, {label});

        const auto allocas = EmitAllocas(function);
        if (!allocas)
            return std::unexpected(allocas.error());

        const IL::Block& block = function.blocks[function.entry];
        for (const IL::Instruction& instruction : block.body)
        {
            if (instruction.op == IL::Op::Alloca)
                continue;
            const auto status = EmitInstruction(function, instruction);
            if (!status)
                return std::unexpected(status.error());
        }

        return EmitTerminator(function, block.terminator);
    }

    [[nodiscard]] Result<void> EmitDispatcher(const IL::Function& function)
    {
        const auto pcConstant = U32Constant(0);
        if (!pcConstant)
            return std::unexpected(pcConstant.error());

        const IL::TypeId u32Type = m_module.types.FindInt(32, false);
        const IL::TypeId pointerTypeId = m_module.types.FindPointer(IL::Space::Private, u32Type);
        if (u32Type == IL::InvalidType || pointerTypeId == IL::InvalidType)
            return std::unexpected(EmitError("SR-SPV-030", "failed to find dispatcher types"));

        const auto u32 = EmitType(u32Type);
        const auto pointerType = EmitType(pointerTypeId);
        if (!u32 || !pointerType)
            return std::unexpected(EmitError("SR-SPV-030", "failed to emit dispatcher types"));

        const std::uint32_t setupLabel = NewId();
        const std::uint32_t headLabel = NewId();
        const std::uint32_t bodyLabel = NewId();
        const std::uint32_t caseMergeLabel = NewId();
        const std::uint32_t continueLabel = NewId();
        const std::uint32_t exitLabel = NewId();
        const std::uint32_t pcVariable = NewId();

        std::vector<std::uint32_t> blockLabels(function.blocks.size(), 0);
        for (std::uint32_t& label : blockLabels)
            label = NewId();

        // setup
        Op(OpLabel, {setupLabel});
        Op(OpVariable, {*pointerType, pcVariable, 7 /* Function */});

        const auto allocas = EmitAllocas(function);
        if (!allocas)
            return std::unexpected(allocas.error());

        const auto spillVariables = EmitSpillVariables(function);
        if (!spillVariables)
            return std::unexpected(spillVariables.error());

        const auto entryConstant = U32Constant(function.entry);
        if (!entryConstant)
            return std::unexpected(entryConstant.error());
        Op(OpStore, {pcVariable, *entryConstant});
        Op(OpBranch, {headLabel});

        // loop head
        Op(OpLabel, {headLabel});
        Op(OpLoopMerge, {exitLabel, continueLabel, 0});
        Op(OpBranch, {bodyLabel});

        // switch on the current block id
        Op(OpLabel, {bodyLabel});
        const std::uint32_t pcValue = NewId();
        Op(OpLoad, {*u32, pcValue, pcVariable});
        Op(OpSelectionMerge, {caseMergeLabel, 0});
        {
            std::vector<std::uint32_t> operands = {pcValue, caseMergeLabel};
            for (IL::BlockId block = 0; block < function.blocks.size(); ++block)
            {
                operands.push_back(block);
                operands.push_back(blockLabels[block]);
            }
            Op(OpSwitch, operands);
        }

        for (IL::BlockId block = 0; block < function.blocks.size(); ++block)
        {
            Op(OpLabel, {blockLabels[block]});
            const auto loads = LoadSpilledForBlock(function, block);
            if (!loads)
                return std::unexpected(loads.error());

            const IL::Block& body = function.blocks[block];
            for (const IL::Instruction& instruction : body.body)
            {
                if (instruction.op == IL::Op::Alloca || instruction.op == IL::Op::Phi)
                    continue;
                const auto status = EmitInstruction(function, instruction);
                if (!status)
                    return std::unexpected(status.error());
            }

            const auto status = EmitDispatchTerminator(function, body.terminator, block, pcVariable, caseMergeLabel);
            if (!status)
                return std::unexpected(status.error());
        }

        Op(OpLabel, {caseMergeLabel});
        Op(OpBranch, {continueLabel});

        Op(OpLabel, {continueLabel});
        Op(OpBranch, {headLabel});

        Op(OpLabel, {exitLabel});
        Op(OpUnreachable, {});
        return {};
    }

    [[nodiscard]] Result<void> EmitDispatchTerminator(const IL::Function& function,
                                                      const IL::Terminator& terminator,
                                                      IL::BlockId sourceBlock,
                                                      std::uint32_t pcVariable,
                                                      std::uint32_t mergeLabel)
    {
        switch (terminator.kind)
        {
        case IL::TerminatorKind::Br:
        {
            const auto phiStores = StorePhiIncoming(function, sourceBlock, terminator.target);
            if (!phiStores)
                return std::unexpected(phiStores.error());

            const auto target = U32Constant(terminator.target);
            if (!target)
                return std::unexpected(target.error());
            Op(OpStore, {pcVariable, *target});
            Op(OpBranch, {mergeLabel});
            return {};
        }
        case IL::TerminatorKind::CondBr:
        {
            const auto condition = ValueId(function, terminator.condition);
            if (!condition)
                return std::unexpected(condition.error());

            const std::uint32_t trueLabel = NewId();
            const std::uint32_t falseLabel = NewId();
            const std::uint32_t joinLabel = NewId();

            Op(OpSelectionMerge, {joinLabel, 0});
            Op(OpBranchConditional, {*condition, trueLabel, falseLabel});

            const auto trueTarget = U32Constant(terminator.trueTarget);
            const auto falseTarget = U32Constant(terminator.falseTarget);
            if (!trueTarget || !falseTarget)
                return std::unexpected(EmitError("SR-SPV-031", "failed to emit branch targets"));

            Op(OpLabel, {trueLabel});
            const auto truePhis = StorePhiIncoming(function, sourceBlock, terminator.trueTarget);
            if (!truePhis)
                return std::unexpected(truePhis.error());
            Op(OpStore, {pcVariable, *trueTarget});
            Op(OpBranch, {joinLabel});

            Op(OpLabel, {falseLabel});
            const auto falsePhis = StorePhiIncoming(function, sourceBlock, terminator.falseTarget);
            if (!falsePhis)
                return std::unexpected(falsePhis.error());
            Op(OpStore, {pcVariable, *falseTarget});
            Op(OpBranch, {joinLabel});

            Op(OpLabel, {joinLabel});
            Op(OpBranch, {mergeLabel});
            return {};
        }
        case IL::TerminatorKind::Switch:
        {
            const auto selector = ValueId(function, terminator.selector);
            if (!selector)
                return std::unexpected(selector.error());

            const std::uint32_t joinLabel = NewId();
            Op(OpSelectionMerge, {joinLabel, 0});

            std::vector<std::uint32_t> operands = {*selector, joinLabel};
            for (const auto& entry : terminator.cases)
            {
                operands.push_back(static_cast<std::uint32_t>(entry.literal));
                operands.push_back(NewId());
            }
            // Rebuild with labels so each case can be emitted after the switch.
            Op(OpSwitch, operands);
            return std::unexpected(EmitError("SR-SPV-032", "switch terminators are not implemented yet", terminator.sourcePc));
        }
        case IL::TerminatorKind::Ret:
            if (terminator.hasReturnValue)
            {
                const auto value = ValueId(function, terminator.returnValue);
                if (!value)
                    return std::unexpected(value.error());
                Op(OpReturnValue, {*value});
            }
            else
            {
                Op(OpReturn, {});
            }
            return {};
        case IL::TerminatorKind::Unreachable:
            Op(OpUnreachable, {});
            return {};
        default:
            return std::unexpected(EmitError("SR-SPV-033", "unsupported terminator", terminator.sourcePc));
        }
    }

    // A guarded instruction is a per-lane predicate the source wrote as EXEC:
    // the hardware skips the access for masked-off lanes, so the module needs a
    // selection around it rather than an unconditional access.
    [[nodiscard]] Result<void> EmitInstruction(const IL::Function& function, const IL::Instruction& instruction)
    {
        if (instruction.guard == IL::InvalidValue)
            return EmitInstructionBody(function, instruction);

        const auto guard = ValueId(function, instruction.guard);
        if (!guard)
            return std::unexpected(EmitError("SR-SPV-080", "invalid instruction guard", instruction.sourcePc));

        const std::uint32_t thenBlock = NewId();
        const std::uint32_t mergeBlock = NewId();
        Op(OpSelectionMerge, {mergeBlock, 0 /* None */});
        Op(OpBranchConditional, {*guard, thenBlock, mergeBlock});
        Op(OpLabel, {thenBlock});

        const auto status = EmitInstructionBody(function, instruction);

        Op(OpBranch, {mergeBlock});
        Op(OpLabel, {mergeBlock});
        return status;
    }

    [[nodiscard]] Result<void> EmitInstructionBody(const IL::Function& function, const IL::Instruction& instruction)
    {
        if (instruction.op == IL::Op::Nop || instruction.op == IL::Op::Alloca)
            return {};

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::SpecialReg))
        {
            const auto* payload = instruction.payload != IL::NoPayload
                                      ? std::get_if<IL::SpecialRegPayload>(&m_module.payloads.Get(instruction.payload))
                                      : nullptr;
            if (payload == nullptr || instruction.results.size() != 1)
                return std::unexpected(EmitError("SR-SPV-065", "special register read without a descriptor", instruction.sourcePc));

            IL::Builtin builtin = IL::Builtin::None;
            std::uint32_t component = 0;
            switch (payload->id)
            {
            case IL::SpecialRegId::ThreadIdX:
                builtin = IL::Builtin::LocalInvocationId;
                component = 0;
                break;
            case IL::SpecialRegId::ThreadIdY:
                builtin = IL::Builtin::LocalInvocationId;
                component = 1;
                break;
            case IL::SpecialRegId::ThreadIdZ:
                builtin = IL::Builtin::LocalInvocationId;
                component = 2;
                break;
            default:
                return std::unexpected(EmitError("SR-SPV-066",
                                                 std::string("special register ") +
                                                     std::string(IL::ToString(payload->id)) + " is not emitted",
                                                 instruction.sourcePc));
            }

            const std::uint32_t variable = SpecialVariableFor(builtin, false);
            if (variable == 0)
                return std::unexpected(EmitError("SR-SPV-067", "special register variable is missing", instruction.sourcePc));

            SpecialVariable entry;
            for (const SpecialVariable& candidate : m_special)
            {
                if (candidate.builtin == builtin)
                    entry = candidate;
            }

            const auto pointer = SpecialComponent(entry, component);
            if (!pointer)
                return std::unexpected(pointer.error());

            const IL::ValueId resultValue = instruction.results[0];
            const auto resultType = EmitType(function.values[resultValue].type);
            if (!resultType)
                return std::unexpected(resultType.error());

            const std::uint32_t resultId =
                (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
            Op(OpLoad, {*resultType, resultId, *pointer});
            m_valueIds[resultValue] = resultId;
            if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                Op(OpStore, {m_spillVars[resultValue], resultId});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::UserData))
        {
            const auto* payload = instruction.payload != IL::NoPayload
                                      ? std::get_if<IL::UserDataDesc>(&m_module.payloads.Get(instruction.payload))
                                      : nullptr;
            if (payload == nullptr || instruction.results.size() != 1 || !m_usePushConstants)
                return std::unexpected(EmitError("SR-SPV-071", "user data read without a push block", instruction.sourcePc));

            const auto index = U32Constant(payload->index);
            if (!index)
                return std::unexpected(index.error());

            const IL::ValueId resultValue = instruction.results[0];
            const auto resultType = EmitType(function.values[resultValue].type);
            if (!resultType)
                return std::unexpected(resultType.error());

            const std::uint32_t pointer = NewId();
            Op(OpAccessChain, {m_pushPtrU32, pointer, m_pushVar, m_pushZero, *index});

            const std::uint32_t resultId =
                (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
            Op(OpLoad, {*resultType, resultId, pointer});
            m_valueIds[resultValue] = resultId;
            if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                Op(OpStore, {m_spillVars[resultValue], resultId});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::EmitVertex) ||
             instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::EndPrimitive)))
        {
            if (m_module.stage != IL::Stage::Geometry)
                return std::unexpected(
                    EmitError("SR-SPV-090", "geometry emission outside a geometry stage", instruction.sourcePc));

            if (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::EmitVertex))
            {
                Op(OpEmitVertex, {});
            }
            else
            {
                Op(OpEndPrimitive, {});
            }
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::ImageLoad) ||
             instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::ImageStore) ||
             instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::ImageSample)))
        {
            const auto* access = instruction.payload != IL::NoPayload
                                     ? std::get_if<IL::ImageAccessDesc>(&m_module.payloads.Get(instruction.payload))
                                     : nullptr;
            if (access == nullptr)
                return std::unexpected(EmitError("SR-SPV-082", "image access without a descriptor", instruction.sourcePc));

            const ImageResource* image = FindImage(access->set, access->binding);
            if (image == nullptr)
                return std::unexpected(EmitError("SR-SPV-083", "image binding is not declared", instruction.sourcePc));

            const auto x = ValueId(function, instruction.operands[0]);
            const auto y = ValueId(function, instruction.operands[1]);
            if (!x || !y)
                return std::unexpected(EmitError("SR-SPV-084", "invalid image coordinates", instruction.sourcePc));

            if (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::ImageSample))
            {
                if (instruction.results.size() != 1)
                    return std::unexpected(EmitError("SR-SPV-088", "image sample must have one result", instruction.sourcePc));
                if (!image->sampled)
                    return std::unexpected(EmitError("SR-SPV-089", "sample on a storage image binding", instruction.sourcePc));

                // Normalized float coordinates arrive as the address VGPR bits.
                const std::uint32_t fx = NewId();
                Op(OpBitcast, {m_imageVec4Element, fx, *x});
                const std::uint32_t fy = NewId();
                Op(OpBitcast, {m_imageVec4Element, fy, *y});
                const std::uint32_t floatCoordinate = NewId();
                Op(OpCompositeConstruct, {m_imageVec2f, floatCoordinate, fx, fy});

                const std::uint32_t imageObject = NewId();
                Op(OpLoad, {image->imageType, imageObject, image->variable});
                const std::uint32_t samplerObject = NewId();
                Op(OpLoad, {image->samplerType, samplerObject, image->samplerVariable});
                const std::uint32_t sampledObject = NewId();
                Op(OpSampledImage, {image->sampledImageType, sampledObject, imageObject, samplerObject});

                const std::uint32_t texel = NewId();
                Op(OpImageSampleExplicitLod, {m_imageVec4, texel, sampledObject, floatCoordinate,
                                              2 /* Lod */, m_imageZeroLod});

                const std::uint32_t component = NewId();
                Op(OpCompositeExtract, {m_imageVec4Element, component, texel,
                                        static_cast<std::uint32_t>(access->component & 3)});

                const IL::ValueId resultValue = instruction.results[0];
                const auto resultType = EmitType(function.values[resultValue].type);
                if (!resultType)
                    return std::unexpected(resultType.error());

                const std::uint32_t resultId =
                    (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
                Op(OpBitcast, {*resultType, resultId, component});
                m_valueIds[resultValue] = resultId;
                if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                    Op(OpStore, {m_spillVars[resultValue], resultId});
                return {};
            }

            const std::uint32_t coordinate = NewId();
            Op(OpCompositeConstruct, {m_imageVec2i, coordinate, *x, *y});

            if (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::ImageLoad))
            {
                if (instruction.results.size() != 1)
                    return std::unexpected(EmitError("SR-SPV-085", "image load must have one result", instruction.sourcePc));

                // OpImageRead takes the image object, so load it from the
                // UniformConstant variable first.
                const std::uint32_t loaded = NewId();
                Op(OpLoad, {image->imageType, loaded, image->variable});

                const std::uint32_t texel = NewId();
                Op(OpImageRead, {m_imageVec4, texel, loaded, coordinate});

                const std::uint32_t component = NewId();
                Op(OpCompositeExtract, {m_imageVec4Element, component, texel,
                                        static_cast<std::uint32_t>(access->component & 3)});

                const IL::ValueId resultValue = instruction.results[0];
                const auto resultType = EmitType(function.values[resultValue].type);
                if (!resultType)
                    return std::unexpected(resultType.error());

                const std::uint32_t resultId =
                    (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
                Op(OpBitcast, {*resultType, resultId, component});
                m_valueIds[resultValue] = resultId;
                if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                    Op(OpStore, {m_spillVars[resultValue], resultId});
                return {};
            }

            if (instruction.operands.size() != 6)
                return std::unexpected(
                    EmitError("SR-SPV-086", "image store needs four component values", instruction.sourcePc));

            std::array<std::uint32_t, 4> components{};
            for (std::uint32_t component = 0; component < 4; ++component)
            {
                const auto value = ValueId(function, instruction.operands[2 + component]);
                if (!value)
                    return std::unexpected(
                        EmitError("SR-SPV-087", "invalid image store component", instruction.sourcePc));
                components[component] = NewId();
                Op(OpBitcast, {m_imageVec4Element, components[component], *value});
            }

            const std::uint32_t loaded = NewId();
            Op(OpLoad, {image->imageType, loaded, image->variable});

            const std::uint32_t texel = NewId();
            Op(OpCompositeConstruct, {m_imageVec4, texel, components[0], components[1], components[2], components[3]});
            Op(OpImageWrite, {loaded, coordinate, texel});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::DeviceLoad) ||
             instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::DeviceStore)))
        {
            if (!m_useDeviceMemory)
                return std::unexpected(EmitError("SR-SPV-073", "device access without the address capability", instruction.sourcePc));

            const auto address = ValueId(function, instruction.operands[0]);
            if (!address)
                return std::unexpected(EmitError("SR-SPV-074", "invalid device address", instruction.sourcePc));

            const std::uint32_t pointer = NewId();
            Op(OpConvertUToPtr, {m_devicePtrU32, pointer, *address});

            if (instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::DeviceStore))
            {
                const auto value = ValueId(function, instruction.operands[1]);
                if (!value)
                    return std::unexpected(EmitError("SR-SPV-075", "invalid device store value", instruction.sourcePc));
                // A physical pointer carries no alignment information, so the
                // access states it: the resources here are dword-aligned.
                Op(OpStore, {pointer, *value, 2 /* Aligned */, 4});
                return {};
            }

            if (instruction.results.size() != 1)
                return std::unexpected(EmitError("SR-SPV-076", "device load must have one result", instruction.sourcePc));

            const IL::ValueId resultValue = instruction.results[0];
            const auto resultType = EmitType(function.values[resultValue].type);
            if (!resultType)
                return std::unexpected(resultType.error());

            const std::uint32_t resultId =
                (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
            Op(OpLoad, {*resultType, resultId, pointer, 2 /* Aligned */, 4});
            m_valueIds[resultValue] = resultId;
            if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                Op(OpStore, {m_spillVars[resultValue], resultId});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::WaveBroadcastFirst))
        {
            if (instruction.results.size() != 1 || instruction.operands.size() != 1)
                return std::unexpected(
                    EmitError("SR-SPV-068", "broadcast-first takes one value and one result", instruction.sourcePc));

            const auto value = ValueId(function, instruction.operands[0]);
            if (!value)
                return std::unexpected(EmitError("SR-SPV-069", "invalid broadcast operand", instruction.sourcePc));

            const IL::ValueId resultValue = instruction.results[0];
            const auto resultType = EmitType(function.values[resultValue].type);
            if (!resultType)
                return std::unexpected(resultType.error());

            const std::uint32_t resultId =
                (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
            Op(OpGroupNonUniformBroadcastFirst, {*resultType, resultId, m_waveScope, *value});
            m_valueIds[resultValue] = resultId;
            if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                Op(OpStore, {m_spillVars[resultValue], resultId});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::WaveBallot))
        {
            if (instruction.results.size() != 1 || instruction.operands.size() != 1)
                return std::unexpected(EmitError("SR-SPV-061", "ballot takes one predicate and one result", instruction.sourcePc));

            const auto predicate = ValueId(function, instruction.operands[0]);
            if (!predicate)
                return std::unexpected(EmitError("SR-SPV-062", "invalid ballot predicate", instruction.sourcePc));

            const IL::ValueId resultValue = instruction.results[0];
            const auto resultType = EmitType(function.values[resultValue].type);
            if (!resultType)
                return std::unexpected(resultType.error());

            const IL::TypeId u32Type = m_module.types.FindInt(32, false);
            const auto u32 = EmitType(u32Type);
            if (!u32)
                return std::unexpected(u32.error());

            const std::uint32_t ballot = NewId();
            Op(OpGroupNonUniformBallot, {m_ballotVecType, ballot, m_waveScope, *predicate});

            const std::uint32_t resultId =
                (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();

            if (m_module.types.Get(function.values[resultValue].type).kind == IL::TypeKind::Int)
            {
                if (m_module.types.Get(function.values[resultValue].type).width == 64)
                {
                    // vec2<u32> is exactly the low 64 bits of the ballot.
                    const std::uint32_t low = NewId();
                    Op(OpCompositeExtract, {*u32, low, ballot, 0});
                    const std::uint32_t high = NewId();
                    Op(OpCompositeExtract, {*u32, high, ballot, 1});
                    const std::uint32_t pair = NewId();
                    Op(OpCompositeConstruct, {m_ballotPairType, pair, low, high});
                    Op(OpBitcast, {*resultType, resultId, pair});
                }
                else
                {
                    Op(OpCompositeExtract, {*resultType, resultId, ballot, 0});
                }
            }
            else
            {
                return std::unexpected(EmitError("SR-SPV-063", "ballot result must be an integer", instruction.sourcePc));
            }

            m_valueIds[resultValue] = resultId;
            if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                Op(OpStore, {m_spillVars[resultValue], resultId});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic)
        {
            const auto intrinsic = static_cast<IL::IntrinsicId>(instruction.subop);
            if (intrinsic == IL::IntrinsicId::StageInput || intrinsic == IL::IntrinsicId::StageOutput)
            {
                const auto* desc = instruction.payload != IL::NoPayload
                                       ? std::get_if<IL::StageIoDesc>(&m_module.payloads.Get(instruction.payload))
                                       : nullptr;
                if (desc == nullptr)
                    return std::unexpected(
                        EmitError("SR-SPV-050", "stage IO without a signature descriptor", instruction.sourcePc));

                const StageIo* binding = FindStageIo(desc->space, desc->builtin, desc->location);
                if (binding == nullptr)
                    return std::unexpected(
                        EmitError("SR-SPV-051", "stage IO is not declared in the module signatures", instruction.sourcePc));
                if (desc->component >= binding->components)
                    return std::unexpected(
                        EmitError("SR-SPV-052", "stage IO component is outside the declared vector", instruction.sourcePc));

                const auto pointer = StageIoPointer(*binding, desc->component);
                if (!pointer)
                    return std::unexpected(pointer.error());

                if (intrinsic == IL::IntrinsicId::StageOutput)
                {
                    const auto value = ValueId(function, instruction.operands[0]);
                    if (!value)
                        return std::unexpected(EmitError("SR-SPV-053", "invalid stage output value", instruction.sourcePc));
                    Op(OpStore, {*pointer, *value});
                    return {};
                }

                if (instruction.results.size() != 1)
                    return std::unexpected(
                        EmitError("SR-SPV-054", "stage input must have one result", instruction.sourcePc));

                const IL::ValueId resultValue = instruction.results[0];
                const auto resultType = EmitType(function.values[resultValue].type);
                if (!resultType)
                    return std::unexpected(resultType.error());

                const std::uint32_t resultId =
                    (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
                Op(OpLoad, {*resultType, resultId, *pointer});
                m_valueIds[resultValue] = resultId;
                if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                    Op(OpStore, {m_spillVars[resultValue], resultId});
                return {};
            }
        }

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::TestBufferStore))
        {
            const auto offset = ValueId(function, instruction.operands[0]);
            const auto value = ValueId(function, instruction.operands[1]);
            if (!offset || !value)
                return std::unexpected(EmitError("SR-SPV-039", "invalid test buffer store", instruction.sourcePc));

            const std::uint32_t pointer = NewId();
            Op(OpAccessChain, {m_bufferPtrU32, pointer, m_bufferVar, m_bufferZero, *offset});
            Op(OpStore, {pointer, *value});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::TestBufferLoad))
        {
            if (instruction.results.size() != 1)
                return std::unexpected(EmitError("SR-SPV-044", "test buffer load must have one result", instruction.sourcePc));
            const auto offset = ValueId(function, instruction.operands[0]);
            if (!offset)
                return std::unexpected(EmitError("SR-SPV-045", "invalid test buffer load offset", instruction.sourcePc));

            const IL::ValueId resultValue = instruction.results[0];
            const auto resultType = EmitType(function.values[resultValue].type);
            if (!resultType)
                return std::unexpected(resultType.error());

            const std::uint32_t pointer = NewId();
            Op(OpAccessChain, {m_bufferPtrU32, pointer, m_bufferVar, m_bufferZero, *offset});
            const std::uint32_t resultId =
                (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
            Op(OpLoad, {*resultType, resultId, pointer});
            m_valueIds[resultValue] = resultId;
            if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                Op(OpStore, {m_spillVars[resultValue], resultId});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::LocalStore))
        {
            const auto offset = ValueId(function, instruction.operands[0]);
            const auto value = ValueId(function, instruction.operands[1]);
            if (!offset || !value)
                return std::unexpected(EmitError("SR-SPV-046", "invalid local store", instruction.sourcePc));

            const std::uint32_t pointer = NewId();
            Op(OpAccessChain, {m_localPtrU32, pointer, m_localVar, *offset});
            Op(OpStore, {pointer, *value});
            return {};
        }

        if (instruction.op == IL::Op::Intrinsic &&
            instruction.subop == static_cast<std::uint16_t>(IL::IntrinsicId::LocalLoad))
        {
            if (instruction.results.size() != 1)
                return std::unexpected(EmitError("SR-SPV-047", "local load must have one result", instruction.sourcePc));
            const auto offset = ValueId(function, instruction.operands[0]);
            if (!offset)
                return std::unexpected(EmitError("SR-SPV-048", "invalid local load offset", instruction.sourcePc));

            const IL::ValueId resultValue = instruction.results[0];
            const auto resultType = EmitType(function.values[resultValue].type);
            if (!resultType)
                return std::unexpected(resultType.error());

            const std::uint32_t pointer = NewId();
            Op(OpAccessChain, {m_localPtrU32, pointer, m_localVar, *offset});
            const std::uint32_t resultId =
                (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();
            Op(OpLoad, {*resultType, resultId, pointer});
            m_valueIds[resultValue] = resultId;
            if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
                Op(OpStore, {m_spillVars[resultValue], resultId});
            return {};
        }

        if (instruction.op == IL::Op::Store)
        {
            const auto pointer = ValueId(function, instruction.operands[0]);
            const auto value = ValueId(function, instruction.operands[1]);
            if (!pointer || !value)
                return std::unexpected(EmitError("SR-SPV-040", "invalid store operands", instruction.sourcePc));
            Op(OpStore, {*pointer, *value});
            return {};
        }

        if (instruction.results.size() != 1)
            return std::unexpected(EmitError("SR-SPV-041", "instruction must have exactly one result", instruction.sourcePc));

        const IL::ValueId resultValue = instruction.results[0];
        const IL::TypeId resultTypeId = function.values[resultValue].type;
        const auto resultType = EmitType(resultTypeId);
        if (!resultType)
            return std::unexpected(resultType.error());

        const IL::Type& resultTypeValue = m_module.types.Get(resultTypeId);
        const std::uint32_t resultId =
            (resultValue < m_valueIds.size() && m_valueIds[resultValue] != 0) ? m_valueIds[resultValue] : NewId();

        switch (instruction.op)
        {
        case IL::Op::Phi:
            // Phis are lowered to spill slots at block boundaries.
            return {};
        case IL::Op::Load:
        {
            const auto pointer = ValueId(function, instruction.operands[0]);
            if (!pointer)
                return std::unexpected(pointer.error());
            Op(OpLoad, {*resultType, resultId, *pointer});
            break;
        }
        case IL::Op::Binary:
        {
            const auto status = EmitBinary(function, instruction, resultTypeValue, *resultType, resultId, resultValue);
            if (!status)
                return status;
            break;
        }
        case IL::Op::Bitcast:
        {
            const auto operand = ValueId(function, instruction.operands[0]);
            if (!operand)
                return std::unexpected(operand.error());
            Op(OpBitcast, {*resultType, resultId, *operand});
            break;
        }
        case IL::Op::Select:
        {
            const auto condition = ValueId(function, instruction.operands[0]);
            const auto ifTrue = ValueId(function, instruction.operands[1]);
            const auto ifFalse = ValueId(function, instruction.operands[2]);
            if (!condition || !ifTrue || !ifFalse)
                return std::unexpected(EmitError("SR-SPV-042", "invalid select operands", instruction.sourcePc));
            Op(OpSelect, {*resultType, resultId, *condition, *ifTrue, *ifFalse});
            break;
        }
        case IL::Op::Compare:
        {
            const auto status = EmitCompare(function, instruction, *resultType, resultId, resultValue);
            if (!status)
                return status;
            break;
        }
        case IL::Op::Convert:
        {
            const auto status = EmitConvert(function, instruction, *resultType, resultId, resultValue);
            if (!status)
                return status;
            break;
        }
        case IL::Op::Unary:
        {
            const auto status = EmitUnary(function, instruction, resultTypeValue, *resultType, resultId, resultValue);
            if (!status)
                return status;
            break;
        }
        case IL::Op::Ternary:
        {
            const auto status = EmitTernary(function, instruction, *resultType, resultId, resultValue);
            if (!status)
                return status;
            break;
        }
        case IL::Op::Atomic:
        case IL::Op::Call:
        case IL::Op::Kill:
        case IL::Op::Intrinsic:
        case IL::Op::Extract:
        case IL::Op::Insert:
        case IL::Op::Construct:
        case IL::Op::Shuffle:
        case IL::Op::Nop:
        case IL::Op::Alloca:
        case IL::Op::Store:
            return std::unexpected(EmitError("SR-SPV-043",
                                             std::string("unsupported IL op in SPIR-V emission: ") +
                                                 std::string(IL::ToString(instruction.op)),
                                             instruction.sourcePc));
        }

        m_valueIds[resultValue] = resultId;
        if (resultValue < m_spilled.size() && m_spilled[resultValue] && !m_isPhi[resultValue])
            Op(OpStore, {m_spillVars[resultValue], resultId});
        return {};
    }

    [[nodiscard]] Result<void> EmitBinary(const IL::Function& function,
                                          const IL::Instruction& instruction,
                                          const IL::Type& resultType,
                                          std::uint32_t typeId,
                                          std::uint32_t resultId,
                                          IL::ValueId resultValue)
    {
        const auto left = ValueId(function, instruction.operands[0]);
        const auto right = ValueId(function, instruction.operands[1]);
        if (!left || !right)
            return std::unexpected(EmitError("SR-SPV-050", "invalid binary operands", instruction.sourcePc));

        const auto subop = static_cast<IL::BinaryOp>(instruction.subop);
        std::uint16_t opcode = 0;

        if (resultType.kind == IL::TypeKind::Float)
        {
            switch (subop)
            {
            case IL::BinaryOp::FAdd:
                opcode = OpFAdd;
                break;
            case IL::BinaryOp::FSub:
                opcode = OpFSub;
                break;
            case IL::BinaryOp::FMul:
                opcode = OpFMul;
                break;
            case IL::BinaryOp::FDiv:
                opcode = OpFDiv;
                break;
            case IL::BinaryOp::FRem:
                opcode = OpFRem;
                break;
            case IL::BinaryOp::FMin:
                Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFMin, *left, *right});
                m_valueIds[resultValue] = resultId;
                return {};
            case IL::BinaryOp::FMax:
                Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFMax, *left, *right});
                m_valueIds[resultValue] = resultId;
                return {};
            default:
                return std::unexpected(EmitError("SR-SPV-051", "unsupported float binary op", instruction.sourcePc));
            }
        }
        else if (resultType.kind == IL::TypeKind::Bool)
        {
            switch (subop)
            {
            case IL::BinaryOp::And:
                opcode = OpLogicalAnd;
                break;
            case IL::BinaryOp::Or:
                opcode = OpLogicalOr;
                break;
            case IL::BinaryOp::Xor:
                opcode = OpLogicalNotEqual;
                break;
            default:
                return std::unexpected(EmitError("SR-SPV-054", "unsupported boolean binary op", instruction.sourcePc));
            }
        }
        else if (resultType.kind == IL::TypeKind::Int)
        {
            switch (subop)
            {
            case IL::BinaryOp::IAdd:
                opcode = OpIAdd;
                break;
            case IL::BinaryOp::ISub:
                opcode = OpISub;
                break;
            case IL::BinaryOp::IMul:
                opcode = OpIMul;
                break;
            case IL::BinaryOp::UDiv:
                opcode = OpUDiv;
                break;
            case IL::BinaryOp::SDiv:
                opcode = OpSDiv;
                break;
            case IL::BinaryOp::URem:
                opcode = OpUMod;
                break;
            case IL::BinaryOp::SRem:
                opcode = OpSRem;
                break;
            case IL::BinaryOp::And:
                opcode = OpBitwiseAnd;
                break;
            case IL::BinaryOp::Or:
                opcode = OpBitwiseOr;
                break;
            case IL::BinaryOp::Xor:
                opcode = OpBitwiseXor;
                break;
            case IL::BinaryOp::Shl:
                opcode = OpShiftLeftLogical;
                break;
            case IL::BinaryOp::LShr:
                opcode = OpShiftRightLogical;
                break;
            case IL::BinaryOp::AShr:
                opcode = OpShiftRightArithmetic;
                break;
            default:
                return std::unexpected(EmitError("SR-SPV-052", "unsupported integer binary op", instruction.sourcePc));
            }
        }
        else
        {
            return std::unexpected(EmitError("SR-SPV-053", "binary op result is not numeric", instruction.sourcePc));
        }

        Op(opcode, {typeId, resultId, *left, *right});
        m_valueIds[resultValue] = resultId;
        return {};
    }

    [[nodiscard]] Result<void> EmitCompare(const IL::Function& function,
                                           const IL::Instruction& instruction,
                                           std::uint32_t typeId,
                                           std::uint32_t resultId,
                                           IL::ValueId resultValue)
    {
        const auto left = ValueId(function, instruction.operands[0]);
        const auto right = ValueId(function, instruction.operands[1]);
        if (!left || !right)
            return std::unexpected(EmitError("SR-SPV-060", "invalid compare operands", instruction.sourcePc));

        const auto subop = static_cast<IL::CompareOp>(instruction.subop);
        std::uint16_t opcode = 0;
        switch (subop)
        {
        case IL::CompareOp::IEq:
        case IL::CompareOp::UEq:
            opcode = OpIEqual;
            break;
        case IL::CompareOp::INe:
        case IL::CompareOp::UNe:
            opcode = OpINotEqual;
            break;
        case IL::CompareOp::ILt:
            opcode = OpSLessThan;
            break;
        case IL::CompareOp::ILe:
            opcode = OpSLessThanEqual;
            break;
        case IL::CompareOp::IGt:
            opcode = OpSGreaterThan;
            break;
        case IL::CompareOp::IGe:
            opcode = OpSGreaterThanEqual;
            break;
        case IL::CompareOp::ULt:
            opcode = OpULessThan;
            break;
        case IL::CompareOp::ULe:
            opcode = OpULessThanEqual;
            break;
        case IL::CompareOp::UGt:
            opcode = OpUGreaterThan;
            break;
        case IL::CompareOp::UGe:
            opcode = OpUGreaterThanEqual;
            break;
        case IL::CompareOp::FOrdEq:
            opcode = OpFOrdEqual;
            break;
        case IL::CompareOp::FOrdNe:
            opcode = OpFOrdNotEqual;
            break;
        case IL::CompareOp::FOrdLt:
            opcode = OpFOrdLessThan;
            break;
        case IL::CompareOp::FOrdLe:
            opcode = OpFOrdLessThanEqual;
            break;
        case IL::CompareOp::FOrdGt:
            opcode = OpFOrdGreaterThan;
            break;
        case IL::CompareOp::FOrdGe:
            opcode = OpFOrdGreaterThanEqual;
            break;
        case IL::CompareOp::FUnordEq:
            opcode = OpFUnordEqual;
            break;
        case IL::CompareOp::FUnordNe:
            opcode = OpFUnordNotEqual;
            break;
        case IL::CompareOp::FUnordLt:
            opcode = OpFUnordLessThan;
            break;
        case IL::CompareOp::FUnordLe:
            opcode = OpFUnordLessThanEqual;
            break;
        case IL::CompareOp::FUnordGt:
            opcode = OpFUnordGreaterThan;
            break;
        case IL::CompareOp::FUnordGe:
            opcode = OpFUnordGreaterThanEqual;
            break;
        }

        Op(opcode, {typeId, resultId, *left, *right});
        m_valueIds[resultValue] = resultId;
        return {};
    }

    [[nodiscard]] Result<void> EmitConvert(const IL::Function& function,
                                           const IL::Instruction& instruction,
                                           std::uint32_t typeId,
                                           std::uint32_t resultId,
                                           IL::ValueId resultValue)
    {
        const auto operand = ValueId(function, instruction.operands[0]);
        if (!operand)
            return std::unexpected(EmitError("SR-SPV-070", "invalid convert operand", instruction.sourcePc));

        std::uint16_t opcode = 0;
        switch (static_cast<IL::ConvertOp>(instruction.subop))
        {
        case IL::ConvertOp::Trunc:
        case IL::ConvertOp::ZExt:
            opcode = OpUConvert;
            break;
        case IL::ConvertOp::SExt:
            opcode = OpSConvert;
            break;
        case IL::ConvertOp::FPTrunc:
        case IL::ConvertOp::FPExt:
            opcode = OpFConvert;
            break;
        case IL::ConvertOp::FPToSI:
            opcode = OpConvertFToS;
            break;
        case IL::ConvertOp::FPToUI:
            opcode = OpConvertFToU;
            break;
        case IL::ConvertOp::SIToFP:
            opcode = OpConvertSToF;
            break;
        case IL::ConvertOp::UIToFP:
            opcode = OpConvertUToF;
            break;
        }

        Op(opcode, {typeId, resultId, *operand});
        m_valueIds[resultValue] = resultId;
        return {};
    }

    [[nodiscard]] Result<void> EmitUnary(const IL::Function& function,
                                         const IL::Instruction& instruction,
                                         const IL::Type& resultType,
                                         std::uint32_t typeId,
                                         std::uint32_t resultId,
                                         IL::ValueId resultValue)
    {
        const auto operand = ValueId(function, instruction.operands[0]);
        if (!operand)
            return std::unexpected(EmitError("SR-SPV-080", "invalid unary operand", instruction.sourcePc));

        std::uint16_t opcode = 0;
        const auto subop = static_cast<IL::UnaryOp>(instruction.subop);
        switch (subop)
        {
        case IL::UnaryOp::Neg:
            opcode = resultType.kind == IL::TypeKind::Float ? OpFNegate : OpSNegate;
            break;
        case IL::UnaryOp::Not:
            opcode = resultType.kind == IL::TypeKind::Bool ? OpLogicalNot : OpNot;
            break;
        case IL::UnaryOp::BitReverse:
            opcode = OpBitReverse;
            break;
        case IL::UnaryOp::PopCount:
            opcode = OpBitCount;
            break;
        case IL::UnaryOp::FAbs:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFAbs, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Sqrt:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslSqrt, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Clz:
        {
            // GLSL FindUMsb returns -1 for a zero input, so 31 - msb yields 32,
            // matching the IL Clz contract (width for zero).
            const std::uint32_t msb = NewId();
            Op(OpExtInst, {typeId, msb, m_glslSet, GlslFindUMsb, *operand});
            const auto thirtyOne = m_u32Constants.find(31);
            if (thirtyOne == m_u32Constants.end())
                return std::unexpected(EmitError("SR-SPV-082", "clz constant was not pre-emitted", instruction.sourcePc));
            Op(OpISub, {typeId, resultId, thirtyOne->second, msb});
            m_valueIds[resultValue] = resultId;
            return {};
        }
        case IL::UnaryOp::Ctz:
            // GLSL FindILsb returns -1 for a zero input, matching the IL path
            // after the zero select that the lifter emits.
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFindILsb, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Rcp:
        {
            const std::uint32_t one =
                resultType.floatFormat == IL::FloatFormat::F16 ? m_f16One
                : resultType.floatFormat == IL::FloatFormat::F64 ? m_f64One
                                                                 : m_f32One;
            if (one == 0)
                return std::unexpected(EmitError("SR-SPV-083", "reciprocal constant was not pre-emitted", instruction.sourcePc));
            Op(OpFDiv, {typeId, resultId, one, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        }
        case IL::UnaryOp::Rsq:
        {
            if (resultType.floatFormat == IL::FloatFormat::F64)
            {
                if (m_f64One == 0)
                    return std::unexpected(EmitError("SR-SPV-084", "rsq constant was not pre-emitted", instruction.sourcePc));
                const std::uint32_t root = NewId();
                Op(OpExtInst, {typeId, root, m_glslSet, GlslSqrt, *operand});
                Op(OpFDiv, {typeId, resultId, m_f64One, root});
                m_valueIds[resultValue] = resultId;
                return {};
            }
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslInverseSqrt, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        }
        case IL::UnaryOp::Exp2:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslExp2, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Log2:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslLog2, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Trunc:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslTrunc, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Ceil:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslCeil, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::RoundEven:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslRoundEven, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Floor:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFloor, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Sin:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslSin, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Cos:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslCos, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        case IL::UnaryOp::Fract:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFract, *operand});
            m_valueIds[resultValue] = resultId;
            return {};
        default:
            return std::unexpected(EmitError("SR-SPV-081", "unsupported unary op", instruction.sourcePc));
        }

        Op(opcode, {typeId, resultId, *operand});
        m_valueIds[resultValue] = resultId;
        return {};
    }

    [[nodiscard]] Result<void> EmitTernary(const IL::Function& function,
                                           const IL::Instruction& instruction,
                                           std::uint32_t typeId,
                                           std::uint32_t resultId,
                                           IL::ValueId resultValue)
    {
        const auto a = ValueId(function, instruction.operands[0]);
        const auto b = ValueId(function, instruction.operands[1]);
        const auto c = ValueId(function, instruction.operands[2]);
        if (!a || !b || !c)
            return std::unexpected(EmitError("SR-SPV-091", "invalid ternary operands", instruction.sourcePc));

        const auto subop = static_cast<IL::TernaryOp>(instruction.subop);
        switch (subop)
        {
        case IL::TernaryOp::Fma:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFma, *a, *b, *c});
            break;
        case IL::TernaryOp::MulAdd:
        {
            const std::uint32_t product = NewId();
            Op(OpFMul, {typeId, product, *a, *b});
            Op(OpFAdd, {typeId, resultId, product, *c});
            break;
        }
        case IL::TernaryOp::FMin3:
        {
            const std::uint32_t first = NewId();
            Op(OpExtInst, {typeId, first, m_glslSet, GlslFMin, *a, *b});
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFMin, first, *c});
            break;
        }
        case IL::TernaryOp::FMax3:
        {
            const std::uint32_t first = NewId();
            Op(OpExtInst, {typeId, first, m_glslSet, GlslFMax, *a, *b});
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFMax, first, *c});
            break;
        }
        case IL::TernaryOp::Clamp:
            Op(OpExtInst, {typeId, resultId, m_glslSet, GlslFClamp, *a, *b, *c});
            break;
        default:
            return std::unexpected(EmitError("SR-SPV-090", "unsupported ternary op", instruction.sourcePc));
        }

        m_valueIds[resultValue] = resultId;
        return {};
    }

    [[nodiscard]] Result<void> EmitTerminator(const IL::Function& function, const IL::Terminator& terminator)
    {
        switch (terminator.kind)
        {
        case IL::TerminatorKind::Ret:
            if (terminator.hasReturnValue)
            {
                const auto value = ValueId(function, terminator.returnValue);
                if (!value)
                    return std::unexpected(value.error());
                Op(OpReturnValue, {*value});
            }
            else
            {
                Op(OpReturn, {});
            }
            return {};
        case IL::TerminatorKind::Unreachable:
            Op(OpUnreachable, {});
            return {};
        case IL::TerminatorKind::Br:
        case IL::TerminatorKind::CondBr:
        case IL::TerminatorKind::Switch:
        case IL::TerminatorKind::IndirectBr:
            return std::unexpected(EmitError("SR-SPV-092", "multi-block function reached the straight-line emitter", terminator.sourcePc));
        }

        return std::unexpected(EmitError("SR-SPV-093", "unknown terminator", terminator.sourcePc));
    }

    const IL::Module& m_module;
    std::vector<std::uint32_t> m_words;
    std::vector<std::uint32_t> m_typeIds;
    std::vector<std::uint32_t> m_valueIds;
    std::unordered_map<std::uint64_t, std::uint32_t> m_constantIds;
    std::unordered_map<std::uint32_t, std::uint32_t> m_u32Constants;
    std::unordered_map<IL::TypeId, std::uint32_t> m_functionPointerTypes;
    std::vector<bool> m_spilled;
    std::vector<bool> m_isPhi;
    std::vector<std::uint32_t> m_spillVars;
    std::vector<std::vector<IL::ValueId>> m_blockUses;
    std::vector<std::unordered_map<IL::ValueId, std::uint32_t>> m_blockLoads;
    IL::BlockId m_currentBlock = IL::InvalidBlockId;
    std::uint32_t m_nextId = 1;
    std::uint32_t m_glslSet = 0;
    bool m_useGlsl = false;
    bool m_useTestBuffer = false;
    bool m_useLocal = false;
    std::uint32_t m_bufferVar = 0;
    std::uint32_t m_bufferPtrU32 = 0;
    std::uint32_t m_localVar = 0;
    std::uint32_t m_localPtrU32 = 0;
    std::uint32_t m_localZero = 0;
    std::uint32_t m_bufferZero = 0;
    std::uint32_t m_mainId = 0;
    std::uint32_t m_f32One = 0;
    std::uint32_t m_f16One = 0;
    std::uint32_t m_f64One = 0;
    std::vector<StageIo> m_stageIo;
    std::vector<SpecialVariable> m_special;
    std::uint32_t m_ballotVecType = 0;
    std::uint32_t m_ballotPairType = 0;
    std::uint32_t m_waveScope = 0;
    bool m_useDeviceMemory = false;
    std::uint32_t m_devicePtrU32 = 0;
    bool m_usePushConstants = false;
    std::uint32_t m_pushVar = 0;
    std::uint32_t m_pushArray = 0;
    std::uint32_t m_pushStruct = 0;
    std::uint32_t m_pushPtrU32 = 0;
    std::uint32_t m_pushZero = 0;
    std::uint32_t m_pushCount = 0;
    std::vector<std::uint32_t> m_pushIndices;

    bool m_structured = false;
    IL::FunctionAnalysis m_analysis;
    std::optional<PostDominators> m_post;
    std::vector<IL::BlockId> m_loopMerge;
    std::vector<IL::BlockId> m_loopContinue;
    std::vector<std::vector<bool>> m_membership;
    std::vector<std::uint32_t> m_blockLabels;
    std::vector<bool> m_emitted;
};

Status Validate(const std::vector<std::uint32_t>& words, std::string& message)
{
    spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_3);
    if (context == nullptr)
    {
        message = "failed to create a SPIR-V context";
        return std::unexpected(Diagnostic::Error("SR-SPV-100", message));
    }

    spv_const_binary_t binary{words.data(), words.size()};
    spv_diagnostic diagnostic = nullptr;
    const spv_result_t result = spvValidate(context, &binary, &diagnostic);

    if (result != SPV_SUCCESS)
    {
        message = diagnostic != nullptr && diagnostic->error != nullptr ? diagnostic->error : "SPIR-V validation failed";
        if (diagnostic != nullptr)
            spvDiagnosticDestroy(diagnostic);
        spvContextDestroy(context);
        return std::unexpected(Diagnostic::Error("SR-SPV-101", message));
    }

    spvContextDestroy(context);
    return {};
}

} // namespace

Result<Emission> Reemit(const IL::Module& module, const TargetProfile& profile)
{
    (void)profile;

    // A geometry input is an array indexed by the input vertex. The emitter
    // does not build that interface yet, so a plain input variable would be
    // silently wrong; name the gap instead.
    if (module.stage == IL::Stage::Geometry && !module.inputs.empty())
        return std::unexpected(EmitError("SR-SPV-094", "geometry input variables are not modeled"));

    // Prefer structured control flow for reducible functions; if the result
    // does not validate, retry with the dispatcher, which handles any CFG.
    {
        Emitter structured(module, true);
        auto emission = structured.Run();
        if (emission)
        {
            std::string message;
            if (Validate(emission->words, message))
                return emission;
        }
    }

    Emitter dispatcher(module, false);
    auto emission = dispatcher.Run();
    if (!emission)
        return std::unexpected(emission.error());

    std::string message;
    const auto status = Validate(emission->words, message);
    if (!status)
        return std::unexpected(status.error());

    return emission;
}

} // namespace SR::Backend
