// Draconic Core - System backend, Win32 implementation.
//
// NOTE: written against SystemBackend.h for Windows/MSVC; not compiled in the
// Linux dev environment. Validate on Windows.

#include "Core/System/SystemBackend.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>   // must precede windows.h (winsock2 vs the legacy winsock.h windows.h pulls in)
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <cstring>

// windows.h #defines these to the ...A/...W variants, which would also rewrite
// our identically-named backend functions. Undo them; we call the A variants.
#undef CreateDirectory
#undef RemoveDirectory
#undef GetEnvironmentVariable   // windows.h maps it to ...A; we define our own and call ...A directly
#undef GetCurrentDirectory      // ditto (GetCurrentDirectoryA)

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

    std::size_t GetEnvironmentVariable(const char* name, char* out, std::size_t outSize) noexcept
    {
        // ::GetEnvironmentVariableA returns chars written (excl null) on success, the required size
        // (incl null) if `out` is too small (nothing written), or 0 if the variable is absent.
        const DWORD n = ::GetEnvironmentVariableA(name, out, static_cast<DWORD>(outSize));
        if (n == 0) { return 0; }
        return (n >= outSize) ? static_cast<std::size_t>(n - 1) : static_cast<std::size_t>(n);
    }

    std::size_t GetUserDataDirectory(char* out, std::size_t outSize) noexcept
    {
        // Per-user, machine-local app data (roaming-free): %LOCALAPPDATA%.
        return GetEnvironmentVariable("LOCALAPPDATA", out, outSize);
    }

    const char* GetHostPlatformName() noexcept { return "Win64"; }
    const char* ExecutableExtension() noexcept { return ".exe"; }

    std::size_t GetExecutablePath(char* out, std::size_t outSize) noexcept
    {
        // GetModuleFileNameA writes the (possibly truncated) null-terminated path and returns the
        // chars written excl null; == outSize when truncated; 0 on error.
        const DWORD n = ::GetModuleFileNameA(nullptr, out, static_cast<DWORD>(outSize));
        return static_cast<std::size_t>(n);
    }

    std::size_t GetCurrentDirectory(char* out, std::size_t outSize) noexcept
    {
        // Fetch into a local buffer (a cwd is well under this), then copy with truncation so the
        // full-length return contract holds even when `out` is too small.
        char buffer[4096];
        const DWORD n = ::GetCurrentDirectoryA(static_cast<DWORD>(sizeof(buffer)), buffer);
        if (n == 0) { return 0; }
        const std::size_t length = static_cast<std::size_t>(n);
        if (out != nullptr && outSize > 0)
        {
            const std::size_t k = (length < outSize - 1) ? length : outSize - 1;
            std::memcpy(out, buffer, k);
            out[k] = '\0';
        }
        return length;
    }

    bool OpenPathInFileManager(const char* path) noexcept
    {
        if (path == nullptr || path[0] == '\0') { return false; }
        // Launch Explorer on the folder via CreateProcess (kernel32) - NOT ShellExecute / a file://
        // URL, both of which can synchronously block or pop a protocol chooser and hang the UI thread.
        // CreateProcess returns as soon as the child starts (we don't wait), so this can never block.
        // Quotes let the path contain spaces; explorer parses its own command line. Core needs no
        // shell32 link this way. Backslash paths are fine (Explorer's native separator).
        char command[4096];
        std::snprintf(command, sizeof(command), "explorer.exe \"%s\"", path);
        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!::CreateProcessA(nullptr, command, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                              &startup, &process))
        {
            return false;
        }
        ::CloseHandle(process.hThread);
        ::CloseHandle(process.hProcess);
        return true;
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

    bool FileCopyPreserving(const char* from, const char* to) noexcept
    {
        // CopyFileA preserves attributes natively (there is no +x bit on Windows).
        return CopyFileA(from, to, FALSE /*overwrite*/) != 0;
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

    // --- UDP sockets (Winsock2; validate on Windows) -----------------------

    namespace { int g_wsaRefs = 0; }

    bool InitializeNetworking() noexcept
    {
        if (g_wsaRefs > 0) { ++g_wsaRefs; return true; }
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { return false; }
        g_wsaRefs = 1;
        return true;
    }
    void ShutdownNetworking() noexcept
    {
        if (g_wsaRefs > 0 && --g_wsaRefs == 0) { WSACleanup(); }
    }

    SocketHandle UdpOpen(std::uint16_t port, std::uint16_t* outBoundPort) noexcept
    {
        const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) { return kInvalidSocket; }
        u_long nonBlocking = 1;
        ::ioctlsocket(s, FIONBIO, &nonBlocking);
        const BOOL yes = TRUE;
        ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { ::closesocket(s); return kInvalidSocket; }
        if (outBoundPort != nullptr)
        {
            sockaddr_in bound{};
            int len = sizeof(bound);
            *outBoundPort = (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len) == 0) ? ntohs(bound.sin_port) : port;
        }
        return static_cast<SocketHandle>(s);
    }

    void SocketClose(SocketHandle socket) noexcept
    {
        if (socket != kInvalidSocket) { ::closesocket(static_cast<SOCKET>(socket)); }
    }

    bool ParseIPv4(const char* dottedQuad, std::uint32_t* outIp) noexcept
    {
        in_addr a{};
        if (::inet_pton(AF_INET, dottedQuad, &a) != 1) { return false; }
        if (outIp != nullptr) { *outIp = ntohl(a.S_un.S_addr); }
        return true;
    }

    std::int64_t UdpSendTo(SocketHandle socket, std::uint32_t ip, std::uint16_t port,
                           const void* data, std::size_t size) noexcept
    {
        if (socket == kInvalidSocket) { return -1; }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(ip);
        addr.sin_port = htons(port);
        const int n = ::sendto(static_cast<SOCKET>(socket), static_cast<const char*>(data), static_cast<int>(size), 0,
                               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (n == SOCKET_ERROR) { return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1; }
        return static_cast<std::int64_t>(n);
    }

    std::int64_t UdpRecvFrom(SocketHandle socket, void* out, std::size_t outCap,
                             std::uint32_t* fromIp, std::uint16_t* fromPort) noexcept
    {
        if (socket == kInvalidSocket) { return -1; }
        sockaddr_in addr{};
        int len = sizeof(addr);
        const int n = ::recvfrom(static_cast<SOCKET>(socket), static_cast<char*>(out), static_cast<int>(outCap), 0,
                                 reinterpret_cast<sockaddr*>(&addr), &len);
        if (n == SOCKET_ERROR) { return (WSAGetLastError() == WSAEWOULDBLOCK) ? 0 : -1; }
        if (fromIp != nullptr) { *fromIp = ntohl(addr.sin_addr.s_addr); }
        if (fromPort != nullptr) { *fromPort = ntohs(addr.sin_port); }
        return static_cast<std::int64_t>(n);
    }
}
