#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "SR/Common/IL/Module.hpp"

namespace SR::IL {

inline constexpr std::size_t NoBlock = static_cast<std::size_t>(-1);

struct NaturalLoop
{
    BlockId header = InvalidBlockId;
    std::vector<BlockId> blocks;
};

struct FunctionAnalysis
{
    std::vector<BlockId> rpo;                     // reachable blocks, reverse postorder
    std::vector<BlockId> idom;                    // per block
    std::vector<std::vector<BlockId>> children;   // dominator tree
    std::vector<std::vector<BlockId>> frontier;   // dominance frontier per block
    std::vector<std::size_t> order;               // block -> rpo position (NoBlock if unreachable)
    std::vector<bool> reachable;
    std::vector<NaturalLoop> loops;

    [[nodiscard]] bool Dominates(BlockId a, BlockId b) const noexcept;
};

// Recomputes predecessors and successors from terminators. Blocks are indexed
// by BlockId (their position in Function::blocks).
void RecomputeEdges(Function& function);

[[nodiscard]] FunctionAnalysis AnalyzeFunction(const Function& function);

[[nodiscard]] std::vector<BlockId> SuccessorsOf(const Terminator& terminator);

} // namespace SR::IL
