#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Common/IL/Module.hpp"

namespace SR::IL {

struct ExecutionResult
{
    std::unordered_map<ValueId, std::uint64_t> memory; // alloca slot -> value
    std::vector<std::uint64_t> values;                 // SSA values at the end
    std::uint64_t steps = 0;
    BlockId finalBlock = InvalidBlockId;
};

// Executes one invocation of a lifted module. This is a reference model for
// tests and the divergence harness, not an optimiser: it implements the IL
// semantics directly, including the register slots left by the lifter.
[[nodiscard]] Result<ExecutionResult> Interpret(const Module& module, std::uint64_t maxSteps = 1u << 20);

} // namespace SR::IL
