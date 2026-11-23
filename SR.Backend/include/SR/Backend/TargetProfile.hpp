#pragma once

#include <cstdint>

namespace SR::Backend {

struct TargetProfile
{
    std::uint32_t spirvMajor = 1;
    std::uint32_t spirvMinor = 6;
    std::uint32_t vulkanMajor = 1;
    std::uint32_t vulkanMinor = 3;

    bool physicalStorageBuffer = true;
    bool cooperativeMatrix = true;
    bool float8 = false;
    bool rayQuery = true;
    bool stochasticRounding = false;
    bool vectorCompression = false;
    bool tensorMemory = false;
};

} // namespace SR::Backend
