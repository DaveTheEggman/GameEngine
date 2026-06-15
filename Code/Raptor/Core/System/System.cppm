// Raptor Core — :system partition
//
// Platform abstraction. Exports thin wrappers that forward to the per-platform
// backend (see SystemBackend.h). Higher-level subsystems (IO, Threading,
// Library) build on these primitives.

module;
#include "Core/Prelude.h"
#include "Core/System/SystemBackend.h"

export module raptor.core:system;

import :base;

export namespace raptor::core
{
    // --- Time --------------------------------------------------------------

    // High-resolution monotonic counter; pair with GetTickFrequency().
    [[nodiscard]] inline u64 GetTicks() noexcept { return sys::GetTicks(); }

    [[nodiscard]] inline u64 GetTickFrequency() noexcept { return sys::GetTickFrequency(); }

    [[nodiscard]] inline f64 TicksToSeconds(u64 ticks) noexcept
    {
        return static_cast<f64>(ticks) / static_cast<f64>(sys::GetTickFrequency());
    }

    [[nodiscard]] inline f64 TicksToMilliseconds(u64 ticks) noexcept
    {
        return TicksToSeconds(ticks) * 1000.0;
    }

    inline void SleepMilliseconds(u32 milliseconds) noexcept
    {
        sys::SleepMilliseconds(milliseconds);
    }

    // --- System info -------------------------------------------------------

    [[nodiscard]] inline u32 LogicalCoreCount() noexcept { return sys::LogicalCoreCount(); }

    [[nodiscard]] inline usize PageSize() noexcept { return sys::PageSize(); }

    // --- Virtual memory (page-granular) ------------------------------------

    // Reserves and commits `size` bytes of page-aligned memory; nullptr on
    // failure. Free with PageFree using the same size.
    [[nodiscard]] inline void* PageAllocate(usize size) noexcept { return sys::PageAllocate(size); }

    inline void PageFree(void* pointer, usize size) noexcept { sys::PageFree(pointer, size); }
}
