#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Common/IL/Module.hpp"
#include "SR/Common/IL/Signature.hpp"

namespace SR::IL {

struct ExecutionResult
{
    std::unordered_map<ValueId, std::uint64_t> memory; // alloca slot -> value
    std::unordered_map<std::uint32_t, std::uint32_t> buffer; // test ABI dword index -> value
    std::unordered_map<std::uint32_t, std::uint32_t> local;  // workgroup-scope dword index -> value
    std::unordered_map<std::uint64_t, std::uint32_t> device; // device address -> dword
    std::unordered_map<std::uint64_t, std::array<std::uint32_t, 4>> images; // image writes
    std::unordered_map<std::uint64_t, std::uint64_t> outputs; // StageIoKey -> value
    // Geometry: the output values captured at each OpEmitVertex, in emission
    // order, which is the sequence the rasterizer receives.
    std::vector<std::unordered_map<std::uint64_t, std::uint64_t>> vertices;
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
    // Stage inputs: StageIoKey -> value. Builtin values use
    // kBuiltinKeyBase + the Builtin enumerator; locations use
    // location * 4 + component.
    std::unordered_map<std::uint64_t, std::uint64_t> stageInputs;
    // Compute invocation state a program reads through special registers.
    std::array<std::uint32_t, 3> threadId = {0, 0, 0};
    std::array<std::uint32_t, 3> workgroupId = {0, 0, 0};
    std::array<std::uint32_t, 3> workgroupSize = {64, 1, 1};
    // User data the dispatch supplies (push constants on the host).
    std::vector<std::uint32_t> userData;
    // Device memory contents keyed by byte address, for buffer descriptors.
    std::unordered_map<std::uint64_t, std::uint32_t> deviceMemory;
    // Image texels: (binding << 40 | y << 20 | x) -> four components.
    std::unordered_map<std::uint64_t, std::array<std::uint32_t, 4>> images;
};

inline constexpr std::uint64_t kBuiltinKeyBase = 0x8000'0000ull;

// The key the interpreter uses for one stage input or output.
[[nodiscard]] constexpr std::uint64_t StageIoKey(Builtin builtin,
                                                 std::uint32_t location,
                                                 std::uint32_t component) noexcept
{
    return builtin != Builtin::None
               ? kBuiltinKeyBase + (static_cast<std::uint64_t>(builtin) * 4) + component
               : static_cast<std::uint64_t>(location) * 4 + component;
}

// Executes one invocation of a lifted module. This is a reference model for
// tests and the divergence harness, not an optimiser: it implements the IL
// semantics directly, including the register slots left by the lifter.
[[nodiscard]] Result<ExecutionResult> Interpret(const Module& module, const ExecutionOptions& options);
[[nodiscard]] Result<ExecutionResult> Interpret(const Module& module, std::uint64_t maxSteps = 1u << 20);

} // namespace SR::IL
