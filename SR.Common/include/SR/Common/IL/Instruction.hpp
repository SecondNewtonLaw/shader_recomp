#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "SR/Common/IL/Attributes.hpp"
#include "SR/Common/IL/Types.hpp"

namespace SR::IL {

using ValueId = std::uint32_t;
using BlockId = std::uint32_t;
using FunctionId = std::uint32_t;
using PayloadId = std::uint32_t;

inline constexpr ValueId InvalidValue = 0xFFFFFFFFu;
inline constexpr BlockId InvalidBlock = 0xFFFFFFFFu;
inline constexpr FunctionId InvalidFunctionId = 0xFFFFFFFFu;
inline constexpr PayloadId NoPayload = 0;

enum class Op : std::uint8_t
{
    Unary,
    Binary,
    Ternary,
    Compare,
    Select,
    Convert,
    Bitcast,
    Extract,
    Insert,
    Construct,
    Shuffle,
    Alloca,
    Load,
    Store,
    Atomic,
    Phi,
    Call,
    Kill,
    Intrinsic,
    Nop,
};

enum class UnaryOp : std::uint16_t
{
    Neg,
    Not,
    FAbs,
    BitReverse,
    PopCount,
    Clz,
    Ctz,
    FindFirstSet,
    Sqrt,
    Fract,
    Rcp,
    Rsq,
    Exp2,
    Log2,
    Trunc,
    Ceil,
    RoundEven,
    Floor,
    Sin,
    Cos,
};

enum class BinaryOp : std::uint16_t
{
    IAdd,
    ISub,
    IMul,
    UDiv,
    SDiv,
    URem,
    SRem,
    FAdd,
    FSub,
    FMul,
    FDiv,
    FRem,
    FMin,
    FMax,
    SMin,
    SMax,
    UMin,
    UMax,
    And,
    Or,
    Xor,
    Shl,
    LShr,
    AShr,
    RotL,
    RotR,
};

enum class TernaryOp : std::uint16_t
{
    Fma,
    IMad,
    FMin3,
    FMax3,
    Clamp,
    Lerp,
    FShl,
    FShr,
    MulAdd,
};

enum class CompareOp : std::uint16_t
{
    IEq,
    INe,
    ILt,
    ILe,
    IGt,
    IGe,
    UEq,
    UNe,
    ULt,
    ULe,
    UGt,
    UGe,
    FOrdEq,
    FOrdNe,
    FOrdLt,
    FOrdLe,
    FOrdGt,
    FOrdGe,
    FUnordEq,
    FUnordNe,
    FUnordLt,
    FUnordLe,
    FUnordGt,
    FUnordGe,
};

enum class ConvertOp : std::uint16_t
{
    Trunc,
    ZExt,
    SExt,
    FPTrunc,
    FPExt,
    FPToSI,
    FPToUI,
    SIToFP,
    UIToFP,
};

enum class ExtractKind : std::uint16_t
{
    // Composite access. The index operand is a constant for the fixed forms.
    Element,
    Member,
    BitField,
};

enum class InsertKind : std::uint16_t
{
    Element,
    Member,
};

enum class ShuffleKind : std::uint16_t
{
    Vector,
    Bit,
    Byte,
};

enum class KillKind : std::uint16_t
{
    Discard = 0,
    Demote = 1,
};

enum class TerminatorKind : std::uint8_t
{
    Br,
    CondBr,
    Switch,
    IndirectBr,
    Ret,
    Unreachable,
};

struct PhiIncoming
{
    ValueId value = InvalidValue;
    BlockId block = InvalidBlock;
};

struct SwitchCase
{
    std::uint64_t literal = 0;
    BlockId target = InvalidBlock;
};

struct Terminator
{
    TerminatorKind kind = TerminatorKind::Unreachable;

    BlockId target = InvalidBlock;      // Br
    ValueId condition = InvalidValue;   // CondBr
    BlockId trueTarget = InvalidBlock;  // CondBr
    BlockId falseTarget = InvalidBlock; // CondBr

    // Switch / IndirectBr
    ValueId selector = InvalidValue;
    BlockId defaultTarget = InvalidBlock;
    std::vector<SwitchCase> cases;
    std::vector<BlockId> targets;

    // Ret
    ValueId returnValue = InvalidValue;
    bool hasReturnValue = false;

    // CondBr / Switch: the condition is wave-uniform.
    bool uniformHint = false;

    std::uint32_t sourcePc = 0;
};

// A single SSA instruction. Results are typed by their Value; operands are
// typed values. `subop` is interpreted by `op`.
struct Instruction
{
    Op op = Op::Nop;
    std::uint16_t subop = 0;
    FunctionId callee = InvalidFunctionId; // Call only
    std::vector<ValueId> results;
    std::vector<ValueId> operands;
    Attrs attrs;
    PayloadId payload = NoPayload;
    std::vector<PhiIncoming> incoming; // Phi only
    std::uint32_t sourcePc = 0;
    std::uint16_t sourceOpcode = 0;
};

[[nodiscard]] std::string_view ToString(Op op) noexcept;
[[nodiscard]] std::string_view ToString(UnaryOp op) noexcept;
[[nodiscard]] std::string_view ToString(BinaryOp op) noexcept;
[[nodiscard]] std::string_view ToString(TernaryOp op) noexcept;
[[nodiscard]] std::string_view ToString(CompareOp op) noexcept;
[[nodiscard]] std::string_view ToString(ConvertOp op) noexcept;
[[nodiscard]] std::string_view ToString(TerminatorKind kind) noexcept;

// Effect classification used by predication lowering and emission.
[[nodiscard]] Effect EffectsOf(Op op) noexcept;

} // namespace SR::IL
