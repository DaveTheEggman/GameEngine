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
import :allocator;
import :string;

namespace raptor::core::detail
{
    // Ensure a StringView is null-terminated for C APIs. If the view is
    // already backed by a null-terminated String, the copy is unnecessary
    // but harmless. A future optimisation could check the trailing byte.
    struct NullTerminated
    {
        String storage;
        explicit NullTerminated(StringView sv) : storage(sv) {}
        [[nodiscard]] const char* CStr() const noexcept
        {
            return reinterpret_cast<const char*>(storage.CStr());
        }
    };

    // Legacy: transcode a wide path to null-terminated UTF-8 for sys:: calls.
    struct NarrowPath
    {
        String storage;
        explicit NarrowPath(WideStringView path) : storage(ToUTF8(path)) {}
        [[nodiscard]] const char* CStr() const noexcept
        {
            return reinterpret_cast<const char*>(storage.CStr());
        }
    };
}

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

    // --- Files (low-level primitives; IO wraps these) ----------------------

    using FileHandle = sys::FileHandle;
    using FileMode = sys::FileMode;
    using SeekOrigin = sys::SeekOrigin;
    inline constexpr FileHandle kInvalidFile = sys::kInvalidFile;

    [[nodiscard]] inline FileHandle FileOpen(StringView path, FileMode mode) noexcept
    {
        return sys::FileOpen(detail::NullTerminated(path).CStr(), mode);
    }

    [[nodiscard]] inline bool FileIsValid(FileHandle handle) noexcept
    {
        return handle != kInvalidFile;
    }

    inline void FileClose(FileHandle handle) noexcept { sys::FileClose(handle); }

    // Returns bytes transferred, or -1 on error.
    [[nodiscard]] inline i64 FileRead(FileHandle handle, void* buffer, u64 bytes) noexcept
    {
        return sys::FileRead(handle, buffer, bytes);
    }

    [[nodiscard]] inline i64 FileWrite(FileHandle handle, const void* buffer, u64 bytes) noexcept
    {
        return sys::FileWrite(handle, buffer, bytes);
    }

    [[nodiscard]] inline i64 FileSeek(FileHandle handle, i64 offset, SeekOrigin origin) noexcept
    {
        return sys::FileSeek(handle, offset, origin);
    }

    [[nodiscard]] inline i64 FileSize(FileHandle handle) noexcept { return sys::FileSize(handle); }

    [[nodiscard]] inline bool FileExists(StringView path) noexcept { return sys::FileExists(detail::NullTerminated(path).CStr()); }

    inline bool FileDelete(StringView path) noexcept { return sys::FileDelete(detail::NullTerminated(path).CStr()); }

    [[nodiscard]] inline bool DirectoryExists(StringView path) noexcept { return sys::DirectoryExists(detail::NullTerminated(path).CStr()); }
    inline bool CreateDirectory(StringView path) noexcept { return sys::CreateDirectory(detail::NullTerminated(path).CStr()); }
    inline bool RemoveDirectory(StringView path) noexcept { return sys::RemoveDirectory(detail::NullTerminated(path).CStr()); }

    // Lists immediate children of a directory, invoking `cb(ctx, name, isDir)`
    // per entry (excluding "." and ".."). `name` is a UTF-8 view valid only for
    // the duration of the call. Returns false if the directory can't be opened.
    using DirEntryCallback = void (*)(void* ctx, StringView name, bool isDirectory);
    inline bool ListDirectory(StringView path, DirEntryCallback cb, void* ctx) noexcept
    {
        struct Bridge { DirEntryCallback cb; void* ctx; } bridge{ cb, ctx };
        return sys::ListDirectory(
            detail::NullTerminated(path).CStr(),
            [](void* c, const char* name, bool isDir) noexcept
            {
                auto* b = static_cast<Bridge*>(c);
                b->cb(b->ctx, StringView(reinterpret_cast<const utf8char*>(name)), isDir);
            },
            &bridge);
    }

    // --- Console -----------------------------------------------------------

    inline void ConsoleWrite(StringView text) noexcept
    {
        sys::ConsoleWrite(reinterpret_cast<const char*>(text.Data()), text.Size());
    }

    inline void ConsoleWriteError(StringView text) noexcept
    {
        sys::ConsoleWriteError(reinterpret_cast<const char*>(text.Data()), text.Size());
    }

    // --- Dynamic libraries (raw; the Library module wraps these) -----------

    using LibraryHandle = sys::LibraryHandle;

    [[nodiscard]] inline LibraryHandle OpenLibrary(StringView path) noexcept { return sys::LibraryOpen(detail::NullTerminated(path).CStr()); }
    [[nodiscard]] inline void* GetLibrarySymbol(LibraryHandle handle, StringView name) noexcept
    {
        return sys::LibrarySymbol(handle, detail::NullTerminated(name).CStr());
    }
    inline void CloseLibrary(LibraryHandle handle) noexcept { sys::LibraryClose(handle); }
}
