#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "SR/Common/Diagnostic.hpp"

namespace SR::Frontend {

struct ElfSection
{
    std::string name;
    std::uint32_t type = 0;
    std::uint64_t flags = 0;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    std::span<const std::uint8_t> data;
};

class ElfContainer
{
public:
    [[nodiscard]] static bool Probe(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] static Result<ElfContainer> Parse(std::span<const std::uint8_t> bytes);

    [[nodiscard]] bool is64() const noexcept;
    [[nodiscard]] std::uint16_t machine() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
    [[nodiscard]] const std::vector<ElfSection>& sections() const noexcept;

    [[nodiscard]] const ElfSection* Find(std::string_view name) const noexcept;
    [[nodiscard]] const ElfSection* FindPrefix(std::string_view prefix) const noexcept;

private:
    std::span<const std::uint8_t> m_bytes;
    std::vector<ElfSection> m_sections;
    bool m_is64 = false;
    std::uint16_t m_machine = 0;
};

} // namespace SR::Frontend
