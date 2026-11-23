#pragma once

#include <cstdint>
#include <string>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Common/IL/Module.hpp"

namespace SR::IL {

struct SsaOptions
{
    // Allocas that are only read and written through Load/Store are promoted.
    // Allocas whose address escapes (or that are indexed) are left in place.
    bool promoteAllocas = true;
};

struct OptimizeOptions
{
    bool foldConstants = true;
    bool eliminateDeadCode = true;
    bool removeUnreachable = true;
};

struct PipelineOptions
{
    bool analyzeCfg = true;
    SsaOptions ssa;
    OptimizeOptions optimize;
};

// Runs the common post-translation chain: CFA -> SSA -> Optimize.
[[nodiscard]] Status RunPipeline(Module& module, PipelineOptions options = {});

// CFA: recompute edges, dominators and the loop forest, and verify the CFG is
// well formed. This is a pure analysis step; it does not rewrite the module.
[[nodiscard]] Status AnalyzeCfg(Module& module);

// SSA: promote register slots (Alloca + Load/Store) to SSA values with phis at
// dominance frontiers. Loads are replaced by the reaching value and the memory
// operations disappear.
[[nodiscard]] Status BuildSsa(Module& module, SsaOptions options = {});

// Optimize: constant folding, dead-code elimination and unreachable-block
// removal over the SSA module.
[[nodiscard]] Status Optimize(Module& module, OptimizeOptions options = {});

} // namespace SR::IL
