#include "SR/Common/Diagnostic.hpp"

#include <utility>

namespace SR {
namespace {

const char* SeverityName(Severity severity)
{
    switch (severity)
    {
    case Severity::Note:
        return "note";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }

    return "error";
}

Diagnostic Make(Severity severity, std::string code, std::string message, std::uint32_t offset)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.offset = offset;
    return diagnostic;
}

} // namespace

Diagnostic Diagnostic::Error(std::string code, std::string message, std::uint32_t offset)
{
    return Make(Severity::Error, std::move(code), std::move(message), offset);
}

Diagnostic Diagnostic::Warning(std::string code, std::string message, std::uint32_t offset)
{
    return Make(Severity::Warning, std::move(code), std::move(message), offset);
}

Diagnostic Diagnostic::Note(std::string code, std::string message, std::uint32_t offset)
{
    return Make(Severity::Note, std::move(code), std::move(message), offset);
}

std::string ToString(const Diagnostic& diagnostic)
{
    std::string text;
    text += SeverityName(diagnostic.severity);
    text += '[';
    text += diagnostic.code;
    text += "]: ";
    text += diagnostic.message;
    text += " (offset ";
    text += std::to_string(diagnostic.offset);
    text += ')';
    return text;
}

} // namespace SR
