#include "SR/Common/IL/Instruction.hpp"

namespace SR::IL {

std::string_view ToString(Op op) noexcept
{
    switch (op)
    {
    case Op::Unary:
        return "unary";
    case Op::Binary:
        return "binary";
    case Op::Ternary:
        return "ternary";
    case Op::Compare:
        return "cmp";
    case Op::Select:
        return "select";
    case Op::Convert:
        return "convert";
    case Op::Bitcast:
        return "bitcast";
    case Op::Extract:
        return "extract";
    case Op::Insert:
        return "insert";
    case Op::Construct:
        return "construct";
    case Op::Shuffle:
        return "shuffle";
    case Op::Alloca:
        return "alloca";
    case Op::Load:
        return "load";
    case Op::Store:
        return "store";
    case Op::Atomic:
        return "atomic";
    case Op::Phi:
        return "phi";
    case Op::Call:
        return "call";
    case Op::Kill:
        return "kill";
    case Op::Intrinsic:
        return "intrinsic";
    case Op::Nop:
        return "nop";
    }

    return "unknown";
}

std::string_view ToString(UnaryOp op) noexcept
{
    switch (op)
    {
    case UnaryOp::Neg:
        return "neg";
    case UnaryOp::Not:
        return "not";
    case UnaryOp::FAbs:
        return "fabs";
    case UnaryOp::BitReverse:
        return "bitreverse";
    case UnaryOp::PopCount:
        return "popcount";
    case UnaryOp::Clz:
        return "clz";
    case UnaryOp::Ctz:
        return "ctz";
    case UnaryOp::FindFirstSet:
        return "ffs";
    case UnaryOp::Sqrt:
        return "sqrt";
    case UnaryOp::Fract:
        return "fract";
    case UnaryOp::Rcp:
        return "rcp";
    case UnaryOp::Rsq:
        return "rsq";
    case UnaryOp::Exp2:
        return "exp2";
    case UnaryOp::Log2:
        return "log2";
    case UnaryOp::Trunc:
        return "trunc";
    case UnaryOp::Ceil:
        return "ceil";
    case UnaryOp::RoundEven:
        return "round-even";
    case UnaryOp::Floor:
        return "floor";
    case UnaryOp::Sin:
        return "sin";
    case UnaryOp::Cos:
        return "cos";
    }

    return "unknown";
}

std::string_view ToString(BinaryOp op) noexcept
{
    switch (op)
    {
    case BinaryOp::IAdd:
        return "iadd";
    case BinaryOp::ISub:
        return "isub";
    case BinaryOp::IMul:
        return "imul";
    case BinaryOp::UDiv:
        return "udiv";
    case BinaryOp::SDiv:
        return "sdiv";
    case BinaryOp::URem:
        return "urem";
    case BinaryOp::SRem:
        return "srem";
    case BinaryOp::FAdd:
        return "fadd";
    case BinaryOp::FSub:
        return "fsub";
    case BinaryOp::FMul:
        return "fmul";
    case BinaryOp::FDiv:
        return "fdiv";
    case BinaryOp::FRem:
        return "frem";
    case BinaryOp::FMin:
        return "fmin";
    case BinaryOp::FMax:
        return "fmax";
    case BinaryOp::SMin:
        return "smin";
    case BinaryOp::SMax:
        return "smax";
    case BinaryOp::UMin:
        return "umin";
    case BinaryOp::UMax:
        return "umax";
    case BinaryOp::And:
        return "and";
    case BinaryOp::Or:
        return "or";
    case BinaryOp::Xor:
        return "xor";
    case BinaryOp::Shl:
        return "shl";
    case BinaryOp::LShr:
        return "lshr";
    case BinaryOp::AShr:
        return "ashr";
    case BinaryOp::RotL:
        return "rotl";
    case BinaryOp::RotR:
        return "rotr";
    }

    return "unknown";
}

std::string_view ToString(TernaryOp op) noexcept
{
    switch (op)
    {
    case TernaryOp::Fma:
        return "fma";
    case TernaryOp::IMad:
        return "imad";
    case TernaryOp::FMin3:
        return "fmin3";
    case TernaryOp::FMax3:
        return "fmax3";
    case TernaryOp::Clamp:
        return "clamp";
    case TernaryOp::Lerp:
        return "lerp";
    case TernaryOp::FShl:
        return "fshl";
    case TernaryOp::FShr:
        return "fshr";
    case TernaryOp::MulAdd:
        return "muladd";
    }

    return "unknown";
}

std::string_view ToString(CompareOp op) noexcept
{
    switch (op)
    {
    case CompareOp::IEq:
        return "ieq";
    case CompareOp::INe:
        return "ine";
    case CompareOp::ILt:
        return "ilt";
    case CompareOp::ILe:
        return "ile";
    case CompareOp::IGt:
        return "igt";
    case CompareOp::IGe:
        return "ige";
    case CompareOp::UEq:
        return "ueq";
    case CompareOp::UNe:
        return "une";
    case CompareOp::ULt:
        return "ult";
    case CompareOp::ULe:
        return "ule";
    case CompareOp::UGt:
        return "ugt";
    case CompareOp::UGe:
        return "uge";
    case CompareOp::FOrdEq:
        return "ford-eq";
    case CompareOp::FOrdNe:
        return "ford-ne";
    case CompareOp::FOrdLt:
        return "ford-lt";
    case CompareOp::FOrdLe:
        return "ford-le";
    case CompareOp::FOrdGt:
        return "ford-gt";
    case CompareOp::FOrdGe:
        return "ford-ge";
    case CompareOp::FUnordEq:
        return "funord-eq";
    case CompareOp::FUnordNe:
        return "funord-ne";
    case CompareOp::FUnordLt:
        return "funord-lt";
    case CompareOp::FUnordLe:
        return "funord-le";
    case CompareOp::FUnordGt:
        return "funord-gt";
    case CompareOp::FUnordGe:
        return "funord-ge";
    }

    return "unknown";
}

std::string_view ToString(ConvertOp op) noexcept
{
    switch (op)
    {
    case ConvertOp::Trunc:
        return "trunc";
    case ConvertOp::ZExt:
        return "zext";
    case ConvertOp::SExt:
        return "sext";
    case ConvertOp::FPTrunc:
        return "fptrunc";
    case ConvertOp::FPExt:
        return "fpext";
    case ConvertOp::FPToSI:
        return "fptosi";
    case ConvertOp::FPToUI:
        return "fptoui";
    case ConvertOp::SIToFP:
        return "sitofp";
    case ConvertOp::UIToFP:
        return "uitofp";
    }

    return "unknown";
}

std::string_view ToString(TerminatorKind kind) noexcept
{
    switch (kind)
    {
    case TerminatorKind::Br:
        return "br";
    case TerminatorKind::CondBr:
        return "condbr";
    case TerminatorKind::Switch:
        return "switch";
    case TerminatorKind::IndirectBr:
        return "indirectbr";
    case TerminatorKind::Ret:
        return "ret";
    case TerminatorKind::Unreachable:
        return "unreachable";
    }

    return "unknown";
}

Effect EffectsOf(Op op) noexcept
{
    switch (op)
    {
    case Op::Load:
        return Effect::ReadMemory;
    case Op::Store:
        return Effect::WriteMemory;
    case Op::Atomic:
        return Effect::ReadMemory | Effect::WriteMemory | Effect::Atomic;
    case Op::Call:
        return Effect::Call;
    case Op::Kill:
        return Effect::Trap;
    case Op::Intrinsic:
        // Intrinsic-specific effects come from the intrinsic spec.
        return Effect::Wave;
    default:
        return Effect::None;
    }
}

} // namespace SR::IL
