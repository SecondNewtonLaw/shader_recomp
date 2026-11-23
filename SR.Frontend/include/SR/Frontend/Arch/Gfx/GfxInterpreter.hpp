#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Frontend/Arch/Gfx/GfxIsa.hpp"

namespace SR::Frontend::Gfx {

// Architectural state for the reference ISA interpreter. VCC is modelled as a
// single-lane predicate, matching the single-invocation IL interpreter and the
// per-invocation SPIR-V that the backend emits.
struct IsaState
{
    std::array<std::uint32_t, 256> sgpr{};
    std::array<std::uint32_t, 256> vgpr{};
    bool scc = false;
    bool vcc = false;
    bool exec = true;
    // Test ABI storage buffer: dword index -> value.
    std::unordered_map<std::uint32_t, std::uint32_t> buffer;
};

struct IsaExecutionResult
{
    IsaState state;
    std::uint64_t steps = 0;
};

// Executes the supported gfx subset directly from the instruction words. It is
// independent of the lifter: it exists to cross-check the IL interpreter and
// the device.
[[nodiscard]] Result<IsaExecutionResult> RunIsa(std::span<const std::uint32_t> words,
                                               Arch arch,
                                               IsaState state = {},
                                               std::uint64_t maxSteps = 1u << 20);

} // namespace SR::Frontend::Gfx
