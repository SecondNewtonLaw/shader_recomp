#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "SR/Common/IL/Attributes.hpp"
#include "SR/Common/IL/Instruction.hpp"
#include "SR/Common/IL/Intrinsics.hpp"
#include "SR/Common/IL/Signature.hpp"
#include "SR/Common/IL/Types.hpp"

namespace SR::IL {

inline constexpr ValueId InvalidValueId = std::numeric_limits<ValueId>::max();
inline constexpr BlockId InvalidBlockId = std::numeric_limits<BlockId>::max();
inline constexpr std::uint32_t InvalidResourceId = std::numeric_limits<std::uint32_t>::max();
// Compatibility alias used by earlier code.
inline constexpr std::uint32_t InvalidId = InvalidValueId;

enum class Stage : std::uint8_t
{
    Unknown,
    Vertex,
    TessControl,
    TessEval,
    Geometry,
    Fragment,
    Compute,
    Mesh,
    Task,
    RayGeneration,
    ClosestHit,
    AnyHit,
    Miss,
    Intersection,
    Callable,
};

constexpr std::string_view ToString(Stage stage)
{
    switch (stage)
    {
    case Stage::Vertex:
        return "vertex";
    case Stage::TessControl:
        return "tess-control";
    case Stage::TessEval:
        return "tess-eval";
    case Stage::Geometry:
        return "geometry";
    case Stage::Fragment:
        return "fragment";
    case Stage::Compute:
        return "compute";
    case Stage::Mesh:
        return "mesh";
    case Stage::Task:
        return "task";
    case Stage::RayGeneration:
        return "ray-generation";
    case Stage::ClosestHit:
        return "closest-hit";
    case Stage::AnyHit:
        return "any-hit";
    case Stage::Miss:
        return "miss";
    case Stage::Intersection:
        return "intersection";
    case Stage::Callable:
        return "callable";
    case Stage::Unknown:
        break;
    }

    return "unknown";
}

struct SignatureDecl
{
    Space space = Space::Input;
    std::uint32_t location = 0;
    std::uint32_t component = 0;
    TypeId type = InvalidType;
    Interpolation interpolation = Interpolation::None;
    bool patch = false;
    Builtin builtin = Builtin::None;
};

struct ResourceDecl
{
    std::uint32_t id = InvalidResourceId;
    ResourceKind kind = ResourceKind::Buffer;
    std::uint32_t set = 0;
    std::uint32_t binding = 0;
    std::uint32_t format = 0;
    std::uint32_t stride = 0;
    TypeId element = InvalidType;
};

enum class RegisterClass : std::uint8_t
{
    Sgpr,
    Vgpr,
    Scc,
    Vcc,
    Exec,
};

// Debug/diagnostic mapping from a source register to its function-local slot.
// It is not part of shader semantics; the interpreter and divergence harness
// use it to observe register state.
struct RegisterSlot
{
    RegisterClass kind = RegisterClass::Sgpr;
    std::uint32_t index = 0;
    ValueId alloca = InvalidValue;
};

// A constant value stored as raw bits. Integers are zero-extended; floats keep
// their IEEE bit pattern (f16/bfloat16 in the low 16 bits).
struct Constant
{
    TypeId type = InvalidType;
    std::uint64_t bits = 0;
};

enum class ValueKind : std::uint8_t
{
    Undef,
    Constant,
    Instruction,
    Parameter,
    Resource,
};

struct Value
{
    TypeId type = InvalidType;
    ValueKind kind = ValueKind::Undef;
    std::uint32_t index = 0; // constant / parameter / resource index
    BlockId block = InvalidBlockId; // defining block, for ValueKind::Instruction
    bool uniform = false;
};

struct Block
{
    std::vector<Instruction> body;
    Terminator terminator;
    std::vector<BlockId> preds;
    std::vector<BlockId> succs;
    std::string hint; // convergence hint, never semantic
};

struct Function
{
    FunctionId id = InvalidFunctionId;
    std::string name;
    std::vector<TypeId> params;
    std::vector<ValueId> paramValues;
    std::vector<Block> blocks;
    BlockId entry = InvalidBlockId;
    TypeId returnType = InvalidType;
    std::vector<Value> values;
    bool isEntry = false;
};

// What one invocation of a geometry stage consumes.
enum class InputTopology : std::uint8_t
{
    Points,
    Lines,
    LinesAdjacency,
    Triangles,
    TrianglesAdjacency,
};

// What a geometry invocation produces.
enum class OutputTopology : std::uint8_t
{
    Points,
    LineStrip,
    TriangleStrip,
};

struct ExecutionModes
{
    std::array<std::uint32_t, 3> workgroupSize = {1, 1, 1};
    // Geometry: the input primitive each invocation sees, the primitive it
    // emits and how many vertices it may emit.
    InputTopology inputTopology = InputTopology::Triangles;
    OutputTopology outputTopology = OutputTopology::TriangleStrip;
    std::uint32_t outputVertices = 3;
    // Geometry: four-component output registers per emitted vertex. A shader
    // that stages vertices in local memory needs this to size the staging.
    std::uint32_t outputRegisters = 0;
    std::uint32_t subgroupSize = 0; // 0 = natural
    bool earlyFragmentTests = false;
    bool depthReplacing = false;
};

class PayloadPool
{
public:
    [[nodiscard]] PayloadId Add(Payload payload);
    [[nodiscard]] const Payload& Get(PayloadId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return m_payloads.size(); }

private:
    std::vector<Payload> m_payloads;
};

struct Module
{
    Stage stage = Stage::Unknown;
    std::string entryPoint;
    std::string architecture;

    TypeTable types;
    std::vector<Constant> constants;
    PayloadPool payloads;
    std::vector<ResourceDecl> resources;
    std::vector<SignatureDecl> inputs;
    std::vector<SignatureDecl> outputs;
    std::vector<RegisterSlot> registerSlots;
    std::vector<Function> functions;
    ExecutionModes executionModes;

    // Test-only ABI: global stores target set 0, binding 0 instead of a V#
    // descriptor. Enabled explicitly by the front end/CLI.
    bool testAbi = false;

    FunctionId entryFunction = InvalidFunctionId;

    [[nodiscard]] Function* FindFunction(FunctionId id) noexcept;
    [[nodiscard]] const Function* FindFunction(FunctionId id) const noexcept;
    [[nodiscard]] Function& GetFunction(FunctionId id);
    [[nodiscard]] const Function& GetFunction(FunctionId id) const;
    [[nodiscard]] const Type& TypeOf(const Function& function, ValueId value) const;

    [[nodiscard]] ValueId AddConstant(TypeId type, std::uint64_t bits);
    [[nodiscard]] ValueId AddUndef(Function& function, TypeId type, bool uniform = false);
    [[nodiscard]] ValueId AddInstructionValue(Function& function,
                                              TypeId type,
                                              BlockId block,
                                              bool uniform = false);
};

} // namespace SR::IL
