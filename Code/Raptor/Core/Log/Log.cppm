// Raptor Core — :log partition
//
// Logging frontend + sinks. Messages are formatted (see :format) into narrow
// char text and dispatched to registered sinks. Level filtering happens up
// front so disabled levels cost almost nothing.
//
// NOTE: the global logger is not yet thread-safe (Threading is not built);
// single-threaded use only for now.

module;
#include "Core/Prelude.h"

export module raptor.core:log;

import :base;
import :memory;
import :array;
import :ring_buffer;
import :format;
import :system;
import :threading;

export namespace raptor::core
{
    enum class LogLevel : u8
    {
        Trace,
        Debug,
        Info,
        Warning,
        Error,
        Fatal,
        Off, // sentinel: filters out everything
    };

    [[nodiscard]] inline const char* LogLevelName(LogLevel level) noexcept
    {
        switch (level)
        {
            case LogLevel::Trace:   return "Trace";
            case LogLevel::Debug:   return "Debug";
            case LogLevel::Info:    return "Info";
            case LogLevel::Warning: return "Warning";
            case LogLevel::Error:   return "Error";
            case LogLevel::Fatal:   return "Fatal";
            case LogLevel::Off:     return "Off";
        }
        return "?";
    }

    // -----------------------------------------------------------------------
    // Sinks
    // -----------------------------------------------------------------------
    class ILogSink
    {
    public:
        virtual ~ILogSink() = default;
        virtual void Write(LogLevel level, const char* category,
                           const char* message, usize length) noexcept = 0;
    };

    namespace detail
    {
        inline void FormatLine(FormatBuffer& line, LogLevel level, const char* category,
                               const char* message, usize length)
        {
            FormatTo(line, "[{}] {}: ", LogLevelName(level), category);
            line.Append(message, length);
            line.Append('\n');
        }
    }

    // Writes to stdout (stderr for Error/Fatal).
    class ConsoleSink final : public ILogSink
    {
    public:
        void Write(LogLevel level, const char* category,
                   const char* message, usize length) noexcept override
        {
            FormatBuffer line;
            detail::FormatLine(line, level, category, message, length);

            if (static_cast<u8>(level) >= static_cast<u8>(LogLevel::Error))
            {
                ConsoleWriteError(line.Data(), line.Size());
            }
            else
            {
                ConsoleWrite(line.Data(), line.Size());
            }
        }
    };

    // Appends to a file.
    class FileSink final : public ILogSink
    {
    public:
        explicit FileSink(const char* path) noexcept { m_file = FileOpen(path, FileMode::Append); }
        ~FileSink() override
        {
            if (FileIsValid(m_file)) { FileClose(m_file); }
        }

        FileSink(const FileSink&) = delete;
        FileSink& operator=(const FileSink&) = delete;

        [[nodiscard]] bool IsOpen() const noexcept { return FileIsValid(m_file); }

        void Write(LogLevel level, const char* category,
                   const char* message, usize length) noexcept override
        {
            if (!FileIsValid(m_file)) { return; }

            FormatBuffer line;
            detail::FormatLine(line, level, category, message, length);
            (void)FileWrite(m_file, line.Data(), line.Size());
        }

    private:
        FileHandle m_file = kInvalidFile;
    };

    // Keeps the most recent messages in a ring buffer (for tools / in-app
    // consoles). Fixed-size records; long category/message text is truncated.
    struct LogRecord
    {
        LogLevel level;
        char category[64];
        char message[192];
    };

    class RingLogSink final : public ILogSink
    {
    public:
        explicit RingLogSink(usize capacity, IAllocator& allocator = DefaultAllocator())
            : m_records(capacity, allocator) {}

        void Write(LogLevel level, const char* category,
                   const char* message, usize length) noexcept override
        {
            LogRecord record{};
            record.level = level;
            CopyTruncated(record.category, sizeof(record.category), category, CStringLen(category));
            CopyTruncated(record.message, sizeof(record.message), message, length);

            if (m_records.IsFull())
            {
                LogRecord discarded;
                m_records.PopFront(discarded);
            }
            m_records.PushBack(record);
        }

        [[nodiscard]] usize Count() const noexcept { return m_records.Size(); }
        [[nodiscard]] const LogRecord& Record(usize index) const noexcept { return m_records[index]; }

    private:
        static usize CStringLen(const char* s) noexcept
        {
            usize n = 0;
            while (s[n] != '\0') { ++n; }
            return n;
        }

        static void CopyTruncated(char* dst, usize dstSize, const char* src, usize srcLen) noexcept
        {
            const usize n = (srcLen < dstSize - 1) ? srcLen : dstSize - 1;
            MemCopy(dst, src, n);
            dst[n] = '\0';
        }

        RingBuffer<LogRecord> m_records;
    };

    // -----------------------------------------------------------------------
    // Logger — owns the sink list and the active level filter.
    // -----------------------------------------------------------------------
    // Thread-safe: an atomic level filters cheaply on the hot path; a mutex
    // guards sink registration and dispatch.
    class Logger
    {
    public:
        // Sinks are non-owning; the caller manages their lifetime.
        void AddSink(ILogSink* sink)
        {
            if (sink == nullptr) { return; }
            ScopedLock lock(m_mutex);
            m_sinks.PushBack(sink);
        }

        void RemoveSink(ILogSink* sink) noexcept
        {
            ScopedLock lock(m_mutex);
            for (usize i = 0; i < m_sinks.Size(); ++i)
            {
                if (m_sinks[i] == sink)
                {
                    m_sinks.RemoveAtSwap(i);
                    return;
                }
            }
        }

        void SetMinLevel(LogLevel level) noexcept { m_minLevel.store(level); }
        [[nodiscard]] LogLevel MinLevel() const noexcept { return m_minLevel.load(); }

        [[nodiscard]] bool IsEnabled(LogLevel level) const noexcept
        {
            return static_cast<u8>(level) >= static_cast<u8>(m_minLevel.load());
        }

        void Dispatch(LogLevel level, const char* category, const char* message, usize length) noexcept
        {
            ScopedLock lock(m_mutex);
            for (ILogSink* sink : m_sinks)
            {
                sink->Write(level, category, message, length);
            }
        }

    private:
        Array<ILogSink*> m_sinks;
        Atomic<LogLevel> m_minLevel{ LogLevel::Info };
        Mutex m_mutex;
    };

    [[nodiscard]] Logger& GlobalLogger() noexcept
    {
        static Logger instance;
        return instance;
    }

    // -----------------------------------------------------------------------
    // Frontend — formats and dispatches (used by the RAPTOR_LOG_* macros).
    // -----------------------------------------------------------------------
    template <typename... Args>
    void Logf(LogLevel level, const char* category, FormatString<Args...> fmt, const Args&... args)
    {
        Logger& logger = GlobalLogger();
        if (!logger.IsEnabled(level))
        {
            return;
        }

        FormatBuffer buffer;
        FormatToV(buffer, fmt.data, args...);
        logger.Dispatch(level, category, buffer.Data(), buffer.Size());
    }
}
