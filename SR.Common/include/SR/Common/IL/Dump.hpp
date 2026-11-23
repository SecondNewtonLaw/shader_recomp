#pragma once

#include <string>

#include "SR/Common/IL/Module.hpp"

namespace SR::IL {

// Deterministic textual dump. IDs follow table order, so equal modules dump
// identically.
[[nodiscard]] std::string DumpModule(const Module& module);
[[nodiscard]] std::string DumpType(const Module& module, TypeId type);

} // namespace SR::IL
