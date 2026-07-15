// Draconic Core - System backend (classic header).
//
// Platform-specific OS services are implemented in per-platform .cpp files
// (System/Linux, System/Win32) as plain external-linkage functions. The
// :system module partition exports thin wrappers that forward here, keeping OS
// headers out of the module BMI. Uses <cstdint>/<cstddef> types so it needs no
// module import; draconic::core's u64/usize are aliases of these exact types.

#ifndef DRACONIC_CORE_SYSTEM_BACKEND_H
#define DRACONIC_CORE_SYSTEM_BACKEND_H

#include <cstddef>
#include <cstdint>

namespace draconic::core::sys
{
    // --- Time --------------------------------------------------------------
    std::uint64_t GetTicks() noexcept;          // high-resolution monotonic counter
    std::uint64_t GetTickFrequency() noexcept;  // counter ticks per second
    void SleepMilliseconds(std::uint32_t milliseconds) noexcept;

    // --- System info -------------------------------------------------------
    std::uint32_t LogicalCoreCount() noexcept;
    std::size_t PageSize() noexcept;

    // --- Environment -------------------------------------------------------
    // Copy environment variable `name` into `out` (truncated to outSize-1, always null-terminated
    // when out/outSize are valid). Returns the value's FULL length excluding the null - so a return
    // >= outSize signals truncation - or 0 when the variable is unset.
    std::size_t GetEnvironmentVariable(const char* name, char* out, std::size_t outSize) noexcept;

    // Platform user-data BASE directory (NO app name appended): $XDG_DATA_HOME or ~/.local/share
    // (Linux), %LOCALAPPDATA% (Windows), ~/Library/Application Support (macOS). Same truncation /
    // return contract as GetEnvironmentVariable; 0 when it cannot be resolved. The :system module
    // wrapper appends the application name.
    std::size_t GetUserDataDirectory(char* out, std::size_t outSize) noexcept;

    // --- Platform identity -------------------------------------------------
    // Host platform tag ("Win64" / "Linux64" / "Mac64"), matching the Bin/<Config>/<Platform> layout.
    const char* GetHostPlatformName() noexcept;
    // Executable filename extension for this platform, WITH the dot (".exe" on Windows, "" elsewhere).
    const char* ExecutableExtension() noexcept;
    // Absolute path of the running executable (readlink /proc/self/exe, GetModuleFileNameA). Same
    // truncation / return contract as GetEnvironmentVariable; 0 on failure.
    std::size_t GetExecutablePath(char* out, std::size_t outSize) noexcept;

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
    // Rename/move a file OR directory (same volume). True on success.
    bool FileMove(const char* from, const char* to) noexcept;
    // File size + last-write time (seconds since epoch). False if the file doesn't exist.
    bool FileStat(const char* path, unsigned long long& outSize, long long& outModifiedTime) noexcept;
    bool DirectoryExists(const char* path) noexcept;
    bool CreateDirectory(const char* path) noexcept;  // true if created or already exists
    bool RemoveDirectory(const char* path) noexcept;

    // Lists the immediate children of a directory, invoking `cb` once per entry
    // (excluding "." and ".."). Allocation-free: the backend owns no buffers.
    // Returns false if the directory can't be opened.
    using DirEntryCallback = void (*)(void* ctx, const char* name, bool isDirectory) noexcept;
    bool ListDirectory(const char* path, DirEntryCallback cb, void* ctx) noexcept;

    // --- Console -----------------------------------------------------------
    void ConsoleWrite(const char* text, std::uint64_t length) noexcept;       // stdout
    void ConsoleWriteError(const char* text, std::uint64_t length) noexcept;   // stderr

    // --- Dynamic libraries (raw; the Library module wraps these) -----------
    using LibraryHandle = void*; // HMODULE on Win32

    LibraryHandle LibraryOpen(const char* path) noexcept;                 // nullptr on failure
    void* LibrarySymbol(LibraryHandle handle, const char* name) noexcept; // nullptr if absent
    void LibraryClose(LibraryHandle handle) noexcept;
}

#endif // DRACONIC_CORE_SYSTEM_BACKEND_H
