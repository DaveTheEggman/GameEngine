// Raptor Core — System backend (classic header).
//
// Platform-specific OS services are implemented in per-platform .cpp files
// (System/Linux, System/Win32) as plain external-linkage functions. The
// :system module partition exports thin wrappers that forward here, keeping OS
// headers out of the module BMI. Uses <cstdint>/<cstddef> types so it needs no
// module import; raptor::core's u64/usize are aliases of these exact types.

#ifndef RAPTOR_CORE_SYSTEM_BACKEND_H
#define RAPTOR_CORE_SYSTEM_BACKEND_H

#include <cstddef>
#include <cstdint>

namespace raptor::core::sys
{
    // --- Time --------------------------------------------------------------
    std::uint64_t GetTicks() noexcept;          // high-resolution monotonic counter
    std::uint64_t GetTickFrequency() noexcept;  // counter ticks per second
    void SleepMilliseconds(std::uint32_t milliseconds) noexcept;

    // --- System info -------------------------------------------------------
    std::uint32_t LogicalCoreCount() noexcept;
    std::size_t PageSize() noexcept;

    // --- Virtual memory (page-granular) ------------------------------------
    void* PageAllocate(std::size_t size) noexcept;   // nullptr on failure
    void PageFree(void* pointer, std::size_t size) noexcept;
}

#endif // RAPTOR_CORE_SYSTEM_BACKEND_H
