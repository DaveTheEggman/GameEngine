// Draconic Core - System backend, Win32 implementation.
//
// NOTE: written against SystemBackend.h for Windows/MSVC; not compiled in the
// Linux dev environment. Validate on Windows.

#include "Core/System/SystemBackend.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>

// windows.h #defines these to the ...A/...W variants, which would also rewrite
// our identically-named backend functions. Undo them; we call the A variants.
#undef CreateDirectory
#undef RemoveDirectory

namespace draconic::core::sys
{
    std::uint64_t GetTicks() noexcept
    {
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        return static_cast<std::uint64_t>(counter.QuadPart);
    }

    std::uint64_t GetTickFrequency() noexcept
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return static_cast<std::uint64_t>(frequency.QuadPart);
    }

    void SleepMilliseconds(std::uint32_t milliseconds) noexcept { Sleep(milliseconds); }

    std::uint32_t LogicalCoreCount() noexcept
    {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return info.dwNumberOfProcessors > 0 ? info.dwNumberOfProcessors : 1u;
    }

    std::size_t PageSize() noexcept
    {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return info.dwPageSize;
    }

    void* PageAllocate(std::size_t size) noexcept
    {
        if (size == 0) { return nullptr; }
        return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }

    void PageFree(void* pointer, std::size_t /*size*/) noexcept
    {
        if (pointer != nullptr) { VirtualFree(pointer, 0, MEM_RELEASE); }
    }

    // --- Files -------------------------------------------------------------
    namespace
    {
        HANDLE ToHandle(FileHandle handle) noexcept { return reinterpret_cast<HANDLE>(handle); }
    }

    FileHandle FileOpen(const char* path, FileMode mode) noexcept
    {
        DWORD access = 0;
        DWORD creation = OPEN_EXISTING;
        switch (mode)
        {
            case FileMode::Read:      access = GENERIC_READ; creation = OPEN_EXISTING; break;
            case FileMode::Write:     access = GENERIC_WRITE; creation = CREATE_ALWAYS; break;
            case FileMode::ReadWrite: access = GENERIC_READ | GENERIC_WRITE; creation = OPEN_ALWAYS; break;
            case FileMode::Append:    access = FILE_APPEND_DATA; creation = OPEN_ALWAYS; break;
        }

        HANDLE handle = CreateFileA(path, access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) { return kInvalidFile; }
        if (mode == FileMode::Append) { SetFilePointer(handle, 0, nullptr, FILE_END); }
        return reinterpret_cast<FileHandle>(handle);
    }

    void FileClose(FileHandle handle) noexcept
    {
        if (handle != kInvalidFile) { CloseHandle(ToHandle(handle)); }
    }

    // NOTE: single calls are capped at DWORD (4 GB); sufficient for buffered IO.
    std::int64_t FileRead(FileHandle handle, void* buffer, std::uint64_t bytes) noexcept
    {
        DWORD read = 0;
        if (!ReadFile(ToHandle(handle), buffer, static_cast<DWORD>(bytes), &read, nullptr)) { return -1; }
        return static_cast<std::int64_t>(read);
    }

    std::int64_t FileWrite(FileHandle handle, const void* buffer, std::uint64_t bytes) noexcept
    {
        DWORD written = 0;
        if (!WriteFile(ToHandle(handle), buffer, static_cast<DWORD>(bytes), &written, nullptr)) { return -1; }
        return static_cast<std::int64_t>(written);
    }

    std::int64_t FileSeek(FileHandle handle, std::int64_t offset, SeekOrigin origin) noexcept
    {
        DWORD method = FILE_BEGIN;
        switch (origin)
        {
            case SeekOrigin::Begin:   method = FILE_BEGIN; break;
            case SeekOrigin::Current: method = FILE_CURRENT; break;
            case SeekOrigin::End:     method = FILE_END; break;
        }
        LARGE_INTEGER distance;
        distance.QuadPart = offset;
        LARGE_INTEGER result;
        if (!SetFilePointerEx(ToHandle(handle), distance, &result, method)) { return -1; }
        return static_cast<std::int64_t>(result.QuadPart);
    }

    std::int64_t FileSize(FileHandle handle) noexcept
    {
        LARGE_INTEGER size;
        if (!GetFileSizeEx(ToHandle(handle), &size)) { return -1; }
        return static_cast<std::int64_t>(size.QuadPart);
    }

    bool FileExists(const char* path) noexcept
    {
        const DWORD attributes = GetFileAttributesA(path);
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    bool FileDelete(const char* path) noexcept { return DeleteFileA(path) != 0; }

    bool FileMove(const char* from, const char* to) noexcept
    {
        return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) != 0;
    }

    bool FileStat(const char* path, unsigned long long& outSize, long long& outModifiedTime) noexcept
    {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &data) == 0
            || (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            return false;
        }
        outSize = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        // FILETIME (100ns since 1601) -> seconds since the unix epoch.
        const unsigned long long ft = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32)
                                    | data.ftLastWriteTime.dwLowDateTime;
        outModifiedTime = static_cast<long long>(ft / 10000000ull) - 11644473600ll;
        return true;
    }

    bool DirectoryExists(const char* path) noexcept
    {
        const DWORD attributes = GetFileAttributesA(path);
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    bool CreateDirectory(const char* path) noexcept
    {
        if (CreateDirectoryA(path, nullptr)) { return true; }
        return DirectoryExists(path);
    }

    bool RemoveDirectory(const char* path) noexcept { return RemoveDirectoryA(path) != 0; }

    bool ListDirectory(const char* path, DirEntryCallback cb, void* ctx) noexcept
    {
        char pattern[MAX_PATH];
        const int n = std::snprintf(pattern, sizeof(pattern), "%s\\*", path);
        if (n < 0 || n >= static_cast<int>(sizeof(pattern))) { return false; }

        WIN32_FIND_DATAA data{};
        HANDLE handle = FindFirstFileA(pattern, &data);
        if (handle == INVALID_HANDLE_VALUE) { return false; }

        do
        {
            const char* name = data.cFileName;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            {
                continue; // skip "." and ".."
            }
            const bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            cb(ctx, name, isDir);
        } while (FindNextFileA(handle, &data));

        FindClose(handle);
        return true;
    }

    void ConsoleWrite(const char* text, std::uint64_t length) noexcept
    {
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, static_cast<DWORD>(length), &written, nullptr);
    }

    void ConsoleWriteError(const char* text, std::uint64_t length) noexcept
    {
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, static_cast<DWORD>(length), &written, nullptr);
    }

    LibraryHandle LibraryOpen(const char* path) noexcept
    {
        return reinterpret_cast<LibraryHandle>(LoadLibraryA(path));
    }

    void* LibrarySymbol(LibraryHandle handle, const char* name) noexcept
    {
        if (handle == nullptr) { return nullptr; }
        // memcpy avoids the function-pointer <-> void* cast warning (MSVC C4054 under /W4 /WX).
        FARPROC proc = GetProcAddress(reinterpret_cast<HMODULE>(handle), name);
        void* result = nullptr;
        std::memcpy(&result, &proc, sizeof(result));
        return result;
    }

    void LibraryClose(LibraryHandle handle) noexcept
    {
        if (handle != nullptr) { FreeLibrary(reinterpret_cast<HMODULE>(handle)); }
    }
}
