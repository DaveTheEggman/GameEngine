// Raptor Core — Debug runtime (classic TU; see Assert.h for the rationale).

#include "Core/Debug/Assert.h"

#include <cstdio>
#include <cstdlib>

namespace raptor::core
{
    namespace
    {
        AssertHandler g_assertHandler = nullptr;
    }

    AssertHandler GetAssertHandler() noexcept
    {
        return g_assertHandler;
    }

    void SetAssertHandler(AssertHandler handler) noexcept
    {
        g_assertHandler = handler;
    }

    bool ReportAssertFailure(const char* expression, const char* message,
                             const char* file, int line, const char* function) noexcept
    {
        if (g_assertHandler != nullptr)
        {
            return g_assertHandler(expression, message, file, line, function);
        }

        std::fprintf(stderr,
                     "\nRaptor assertion failed\n"
                     "  expression: %s\n"
                     "  message   : %s\n"
                     "  location  : %s:%d\n"
                     "  function  : %s\n",
                     expression,
                     (message != nullptr) ? message : "(none)",
                     file, line, function);
        std::fflush(stderr);
        return true; // break into the debugger / trap
    }

    void ReportFatal(const char* message, const char* file, int line, const char* function) noexcept
    {
        std::fprintf(stderr,
                     "\nRaptor fatal error\n"
                     "  message : %s\n"
                     "  location: %s:%d\n"
                     "  function: %s\n",
                     message, file, line, function);
        std::fflush(stderr);
        std::abort();
    }
}
