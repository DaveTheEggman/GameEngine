// Raptor Core — Debug / assertions
//
// Assertions are macro-based, so they live in a classic header (macros cannot
// be exported by modules). The reporting functions have external linkage in the
// global module, which lets *any* translation unit — including other module
// units' global-module-fragments (e.g. :base) — include this and call them with
// no module-import cycle.
//
// Usage: `#include "Core/Debug/Assert.h"` in a global module fragment.
//
//   RAPTOR_ASSERT(cond)        debug-only; report + break on failure
//   RAPTOR_ASSERT_MSG(c, msg)  ditto, with a message
//   RAPTOR_VERIFY(cond)        condition always evaluated; checked unless shipping
//   RAPTOR_CHECK(cond)         always-on assert (all build configs)
//   RAPTOR_ENSURE(cond)        non-fatal; reports once on failure, returns the bool
//   RAPTOR_UNREACHABLE()       fatal; marks unreachable code

#ifndef RAPTOR_CORE_DEBUG_ASSERT_H
#define RAPTOR_CORE_DEBUG_ASSERT_H

#include "Core/Prelude.h"

namespace raptor::core
{
    // Reports a failed assertion. Returns true if the caller should break into
    // the debugger / trap. Plain const char* keeps this dependency-free and
    // safe to include in any global module fragment.
    bool ReportAssertFailure(const char* expression, const char* message,
                             const char* file, int line, const char* function) noexcept;

    // Reports a fatal error and terminates. Never returns.
    [[noreturn]] void ReportFatal(const char* message,
                                  const char* file, int line, const char* function) noexcept;

    // Custom assertion handler. Return true to break/trap, false to continue.
    using AssertHandler = bool (*)(const char* expression, const char* message,
                                   const char* file, int line, const char* function) noexcept;

    AssertHandler GetAssertHandler() noexcept;
    void SetAssertHandler(AssertHandler handler) noexcept;
}

#if RAPTOR_COMPILER_MSVC && !RAPTOR_COMPILER_CLANG
    #define RAPTOR_DEBUGBREAK() __debugbreak()
#else
    #define RAPTOR_DEBUGBREAK() __builtin_trap()
#endif

// Core check expression: evaluates `cond`; on failure reports and, if the
// handler requests it, breaks. Yields void.
#define RAPTOR_ASSERT_IMPL(cond, msg)                                                   \
    (RAPTOR_LIKELY(!!(cond))                                                            \
         ? (void)0                                                                      \
         : (::raptor::core::ReportAssertFailure(#cond, (msg), __FILE__, __LINE__, __func__) \
                ? RAPTOR_DEBUGBREAK()                                                   \
                : (void)0))

#if RAPTOR_SHIPPING
    #define RAPTOR_ASSERT(cond)          ((void)0)
    #define RAPTOR_ASSERT_MSG(cond, msg) ((void)0)
    #define RAPTOR_VERIFY(cond)          ((void)(cond))
#else
    #define RAPTOR_ASSERT(cond)          RAPTOR_ASSERT_IMPL(cond, nullptr)
    #define RAPTOR_ASSERT_MSG(cond, msg) RAPTOR_ASSERT_IMPL(cond, msg)
    #define RAPTOR_VERIFY(cond)          RAPTOR_ASSERT_IMPL(cond, nullptr)
#endif

// Always-on, every build configuration.
#define RAPTOR_CHECK(cond)          RAPTOR_ASSERT_IMPL(cond, nullptr)
#define RAPTOR_CHECK_MSG(cond, msg) RAPTOR_ASSERT_IMPL(cond, msg)

// Non-fatal: reports on failure but does not break; evaluates to the condition,
// so it composes:  if (!RAPTOR_ENSURE(ptr != nullptr)) { return; }
#define RAPTOR_ENSURE(cond)                                                             \
    (RAPTOR_LIKELY(!!(cond))                                                            \
         ? true                                                                         \
         : (::raptor::core::ReportAssertFailure(#cond, nullptr, __FILE__, __LINE__, __func__), \
            false))

#define RAPTOR_UNREACHABLE() \
    (::raptor::core::ReportFatal("reached unreachable code", __FILE__, __LINE__, __func__))

#endif // RAPTOR_CORE_DEBUG_ASSERT_H
