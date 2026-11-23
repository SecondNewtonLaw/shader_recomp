#pragma once

#include <string>
#include <vector>

#include "SR/Common/Diagnostic.hpp"
#include "SR/Common/IL/Module.hpp"

namespace SR::IL {

struct VerificationOptions
{
    bool checkDominance = true;
    bool checkCalls = true;
    bool checkIntrinsics = true;
};

// Diagnostics are appended to `issues` (if provided) and the first error is
// returned. A module with warnings is still valid.
[[nodiscard]] Status VerifyModule(const Module& module,
                                  std::vector<Diagnostic>* issues = nullptr,
                                  VerificationOptions options = {});

[[nodiscard]] bool IsTerminatorless(const Block& block) noexcept;

} // namespace SR::IL
