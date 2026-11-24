#include "SR/Common/Assertions.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>

#include <libassert/assert.hpp>

#ifdef _WIN32
#include <windows.h>

#include <dbghelp.h>
#endif

namespace SR {
namespace {

std::string g_application = "sr";
AssertionAction g_action = AssertionAction::Abort;

void Print(std::string_view text) noexcept
{
    std::fwrite(text.data(), 1, text.size(), stderr);
    std::fflush(stderr);
}

#ifdef _WIN32
void PrintStack() noexcept
{
    void* frames[48] = {};
    const USHORT count = CaptureStackBackTrace(1, 48, frames, nullptr);
    HANDLE process = GetCurrentProcess();
    const bool symbols = SymInitialize(process, nullptr, TRUE) != FALSE;

    for (USHORT index = 0; index < count; ++index)
    {
        char line[512] = {};
        if (symbols)
        {
            alignas(SYMBOL_INFO) std::uint8_t storage[sizeof(SYMBOL_INFO) + 256] = {};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 255;

            DWORD64 displacement = 0;
            if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[index]), &displacement, symbol) != FALSE)
            {
                IMAGEHLP_LINE64 location{};
                location.SizeOfStruct = sizeof(location);
                DWORD offset = 0;
                if (SymGetLineFromAddr64(process, reinterpret_cast<DWORD64>(frames[index]), &offset, &location) != FALSE)
                {
                    std::snprintf(line, sizeof(line), "  #%02u %s at %s:%lu\n", index, symbol->Name,
                                  location.FileName != nullptr ? location.FileName : "?", location.LineNumber);
                }
                else
                {
                    std::snprintf(line, sizeof(line), "  #%02u %s\n", index, symbol->Name);
                }
            }
        }

        if (line[0] == '\0')
            std::snprintf(line, sizeof(line), "  #%02u %p\n", index, frames[index]);
        Print(line);
    }

    if (symbols)
        SymCleanup(process);
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* info) noexcept
{
    const auto* record = info != nullptr ? info->ExceptionRecord : nullptr;
    char header[256] = {};
    if (record != nullptr)
    {
        std::snprintf(header,
                      sizeof(header),
                      "[%s] unhandled exception 0x%08lX at %p\n",
                      g_application.c_str(),
                      record->ExceptionCode,
                      record->ExceptionAddress);
    }
    else
    {
        std::snprintf(header, sizeof(header), "[%s] unhandled exception\n", g_application.c_str());
    }
    Print(header);

    PrintStack();

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void TerminateHandler() noexcept
{
    if (const std::exception_ptr current = std::current_exception())
    {
        try
        {
            std::rethrow_exception(current);
        }
        catch (const std::exception& error)
        {
            Print(std::string("[") + g_application + "] terminated: " + error.what() + "\n");
        }
        catch (...)
        {
            Print(std::string("[") + g_application + "] terminated: unknown exception\n");
        }
    }
    else
    {
        Print(std::string("[") + g_application + "] terminated\n");
    }

    std::abort();
}

} // namespace

void InstallAssertionHandler(std::string_view application, AssertionAction action)
{
    g_application = std::string(application);
    g_action = action;

    std::set_terminate(TerminateHandler);

#if defined(_WIN32)
    SetUnhandledExceptionFilter(CrashFilter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, [](int) {
        Print(std::string("[") + g_application + "] abort\n");
        PrintStack();
        std::_Exit(3);
    });
#endif

    libassert::set_failure_handler([](const libassert::assertion_info& info) {
        std::string message = "[" + g_application + "] " + std::string(info.action()) + " at " + std::string(info.location());
        if (info.message)
            message += ": " + *info.message;
        message += "\n  " + std::string(info.expression_string) + "\n";
        Print(message);

        if (g_action == AssertionAction::Throw)
            throw std::runtime_error(message);
        std::abort();
    });
}

} // namespace SR
