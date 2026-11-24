#include <cstdint>
#include <string_view>

namespace SR {

// Installs the process-wide failure path: libassert assertions print their
// location and expression instead of trapping, uncaught exceptions print
// what(), and (on Windows) an unhandled exception prints its code, address and
// a symbolized stack instead of dying silently.
//
// Tests call this with AssertionAction::Throw so the harness reports the
// assertion as a test failure; tools use Abort so a broken invariant stops the
// process after the message.
enum class AssertionAction : std::uint8_t
{
    Throw,
    Abort,
};

void InstallAssertionHandler(std::string_view application, AssertionAction action = AssertionAction::Abort);

} // namespace SR
