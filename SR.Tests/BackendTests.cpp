#include <catch2/catch_test_macros.hpp>

#include "SR/Backend/Reemitter.hpp"
#include "SR/Common/IL/Module.hpp"

using namespace SR;

TEST_CASE("Backend rejects a module without an entry function", "[backend]")
{
    IL::Module module;
    module.stage = IL::Stage::Compute;
    module.entryPoint = "main";

    const auto result = Backend::Reemit(module);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().severity == Severity::Error);
    REQUIRE(result.error().code == "SR-SPV-020");
}
