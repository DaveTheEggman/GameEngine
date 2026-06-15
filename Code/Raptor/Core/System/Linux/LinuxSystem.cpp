// Raptor Core — System backend, Linux implementation.

#include "Core/System/SystemBackend.h"

#include <ctime>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace raptor::core::sys
{
    std::uint64_t GetTicks() noexcept
    {
        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull
             + static_cast<std::uint64_t>(ts.tv_nsec);
    }

    std::uint64_t GetTickFrequency() noexcept
    {
        return 1'000'000'000ull; // GetTicks() is in nanoseconds
    }

    void SleepMilliseconds(std::uint32_t milliseconds) noexcept
    {
        timespec req{};
        req.tv_sec = static_cast<time_t>(milliseconds / 1000u);
        req.tv_nsec = static_cast<long>((milliseconds % 1000u) * 1'000'000ull);

        timespec rem{};
        while (nanosleep(&req, &rem) == -1)
        {
            req = rem; // interrupted by a signal — resume for the remainder
        }
    }

    std::uint32_t LogicalCoreCount() noexcept
    {
        const long count = sysconf(_SC_NPROCESSORS_ONLN);
        return (count > 0) ? static_cast<std::uint32_t>(count) : 1u;
    }

    std::size_t PageSize() noexcept
    {
        const long size = sysconf(_SC_PAGESIZE);
        return (size > 0) ? static_cast<std::size_t>(size) : 4096u;
    }

    void* PageAllocate(std::size_t size) noexcept
    {
        if (size == 0)
        {
            return nullptr;
        }

        void* memory = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (memory == MAP_FAILED) ? nullptr : memory;
    }

    void PageFree(void* pointer, std::size_t size) noexcept
    {
        if (pointer != nullptr && size != 0)
        {
            munmap(pointer, size);
        }
    }
}
