#pragma once

#include <cstdint>
#include <vector>

#include "SR/Backend/TargetProfile.hpp"
#include "SR/Common/Diagnostic.hpp"
#include "SR/Common/IL/Module.hpp"

namespace SR::Backend {

struct Emission
{
    std::vector<std::uint32_t> words;
};

[[nodiscard]] Result<Emission> Reemit(const IL::Module& module, const TargetProfile& profile = {});

} // namespace SR::Backend
