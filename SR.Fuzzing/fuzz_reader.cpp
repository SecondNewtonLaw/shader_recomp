#include <cstddef>
#include <cstdint>

#include "SR/Frontend/MasterReader.hpp"

// libFuzzer entry point. Any input may be malformed; the readers must return a
// diagnostic, not crash, hang or over-read. Sanitizers catch the rest.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    constexpr std::size_t kMaxInput = 8u * 1024u * 1024u;
    if (size > kMaxInput)
        return 0;

    static SR::Frontend::MasterReader reader;
    const auto result = reader.Read({data, size});
    (void)result;
    return 0;
}
