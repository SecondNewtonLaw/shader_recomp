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
    std::unordered_map<std::uint32_t, std::uint32_t> buffer; // test ABI dword index -> value
    std::unordered_map<std::uint32_t, std::uint32_t> local;  // workgroup-scope dword index -> value
    std::vector<std::uint64_t> values;                 // SSA values at the end
    std::uint64_t steps = 0;
    BlockId finalBlock = InvalidBlockId;
};

struct ExecutionOptions
{
    std::uint64_t maxSteps = 1u << 20;
    // Test ABI storage buffer contents: dword index -> value.
    std::unordered_map<std::uint32_t, std::uint32_t> testBuffer;
    // Workgroup-scope memory contents: dword index -> value.
    std::unordered_map<std::uint32_t, std::uint32_t> localMemory;
};

// Executes one invocation of a lifted module. This is a reference model for
// tests and the divergence harness, not an optimiser: it implements the IL
// semantics directly, including the register slots left by the lifter.
[[nodiscard]] Result<ExecutionResult> Interpret(const Module& module, const ExecutionOptions& options);
[[nodiscard]] Result<ExecutionResult> Interpret(const Module& module, std::uint64_t maxSteps = 1u << 20);

} // namespace SR::IL
