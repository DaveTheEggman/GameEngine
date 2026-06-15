// Raptor Core — System backend, Linux implementation.

#include "Core/System/SystemBackend.h"

#include <ctime>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
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

    FileHandle FileOpen(const char* path, FileMode mode) noexcept
    {
        int flags = 0;
        switch (mode)
        {
            case FileMode::Read:      flags = O_RDONLY; break;
            case FileMode::Write:     flags = O_WRONLY | O_CREAT | O_TRUNC; break;
            case FileMode::ReadWrite: flags = O_RDWR | O_CREAT; break;
            case FileMode::Append:    flags = O_WRONLY | O_CREAT | O_APPEND; break;
        }

        const int fd = open(path, flags, 0644);
        return (fd < 0) ? kInvalidFile : static_cast<FileHandle>(fd);
    }

    void FileClose(FileHandle handle) noexcept
    {
        if (handle != kInvalidFile)
        {
            close(static_cast<int>(handle));
        }
    }

    std::int64_t FileRead(FileHandle handle, void* buffer, std::uint64_t bytes) noexcept
    {
        const ssize_t n = read(static_cast<int>(handle), buffer, static_cast<std::size_t>(bytes));
        return static_cast<std::int64_t>(n);
    }

    std::int64_t FileWrite(FileHandle handle, const void* buffer, std::uint64_t bytes) noexcept
    {
        const ssize_t n = write(static_cast<int>(handle), buffer, static_cast<std::size_t>(bytes));
        return static_cast<std::int64_t>(n);
    }

    std::int64_t FileSeek(FileHandle handle, std::int64_t offset, SeekOrigin origin) noexcept
    {
        int whence = SEEK_SET;
        switch (origin)
        {
            case SeekOrigin::Begin:   whence = SEEK_SET; break;
            case SeekOrigin::Current: whence = SEEK_CUR; break;
            case SeekOrigin::End:     whence = SEEK_END; break;
        }

        const off_t pos = lseek(static_cast<int>(handle), static_cast<off_t>(offset), whence);
        return static_cast<std::int64_t>(pos);
    }

    std::int64_t FileSize(FileHandle handle) noexcept
    {
        struct stat st{};
        if (fstat(static_cast<int>(handle), &st) != 0)
        {
            return -1;
        }
        return static_cast<std::int64_t>(st.st_size);
    }

    bool FileExists(const char* path) noexcept
    {
        return access(path, F_OK) == 0;
    }

    bool FileDelete(const char* path) noexcept
    {
        return unlink(path) == 0;
    }

    void ConsoleWrite(const char* text, std::uint64_t length) noexcept
    {
        ssize_t result = write(STDOUT_FILENO, text, static_cast<std::size_t>(length));
        (void)result;
    }

    void ConsoleWriteError(const char* text, std::uint64_t length) noexcept
    {
        ssize_t result = write(STDERR_FILENO, text, static_cast<std::size_t>(length));
        (void)result;
    }
}
