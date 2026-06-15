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

    // --- Files (low-level primitives; IO wraps these) ----------------------
    // Opaque handle: fd on Linux, HANDLE on Win32. kInvalidFile on failure.
    using FileHandle = std::intptr_t;
    inline constexpr FileHandle kInvalidFile = -1;

    enum class FileMode
    {
        Read,       // existing file, read-only
        Write,      // create/truncate, write-only
        ReadWrite,  // create if needed, read+write
        Append,     // create if needed, write at end
    };

    enum class SeekOrigin
    {
        Begin,
        Current,
        End,
    };

    FileHandle FileOpen(const char* path, FileMode mode) noexcept;
    void FileClose(FileHandle handle) noexcept;
    std::int64_t FileRead(FileHandle handle, void* buffer, std::uint64_t bytes) noexcept;   // -1 on error
    std::int64_t FileWrite(FileHandle handle, const void* buffer, std::uint64_t bytes) noexcept;
    std::int64_t FileSeek(FileHandle handle, std::int64_t offset, SeekOrigin origin) noexcept; // new pos, -1 error
    std::int64_t FileSize(FileHandle handle) noexcept;                                      // -1 on error
    bool FileExists(const char* path) noexcept;
    bool FileDelete(const char* path) noexcept;

    // --- Console -----------------------------------------------------------
    void ConsoleWrite(const char* text, std::uint64_t length) noexcept;       // stdout
    void ConsoleWriteError(const char* text, std::uint64_t length) noexcept;   // stderr
}

#endif // RAPTOR_CORE_SYSTEM_BACKEND_H
