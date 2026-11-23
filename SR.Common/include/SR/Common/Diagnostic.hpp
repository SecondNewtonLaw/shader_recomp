#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace SR {

enum class Severity : std::uint8_t
{
    Note,
    Warning,
    Error,
};

struct Diagnostic
{
    Severity severity = Severity::Error;
    std::string code;
    std::string message;
    std::uint32_t offset = 0;

    static Diagnostic Error(std::string code, std::string message, std::uint32_t offset = 0);
    static Diagnostic Warning(std::string code, std::string message, std::uint32_t offset = 0);
    static Diagnostic Note(std::string code, std::string message, std::uint32_t offset = 0);
};

template <typename T>
using Result = std::expected<T, Diagnostic>;

using Status = Result<void>;

std::string ToString(const Diagnostic& diagnostic);

} // namespace SR
