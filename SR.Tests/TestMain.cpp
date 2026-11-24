#include <catch2/catch_session.hpp>

#include "SR/Common/Assertions.hpp"

int main(int argc, char** argv)
{
    // Installed before the harness runs: an assertion prints its location and
    // expression and is reported as a test failure instead of trapping.
    SR::InstallAssertionHandler("SR.Tests", SR::AssertionAction::Throw);
    return Catch::Session().run(argc, argv);
}
