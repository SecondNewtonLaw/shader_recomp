#include <catch2/catch_test_macros.hpp>

#include "SR/Frontend/Frontend.hpp"

TEST_CASE("Frontend library reports a version", "[frontend]")
{
    REQUIRE_FALSE(SR::Frontend::LibraryVersion().empty());
}
