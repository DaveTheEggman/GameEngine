// Raptor Core — :logger partition
//
// Logging frontend: log levels, the ILogSink interface, the Logger (sink list
// + level filter) and the Logf frontend behind the RAPTOR_LOG_* macros.
// Concrete sinks (Console/File/Ring) live in their own partitions.
//
// Thread-safe: an atomic level filters cheaply on the hot path; a mutex guards
// sink registration and dispatch.

module;
#include "Core/Prelude.h"

export module raptor.core:logger;

import :base;
import :array;
import :string;
import :format;
import :atomic;
import :mutex;
import :scoped_lock;

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

    [[nodiscard]] inline const widechar* LogLevelName(LogLevel level) noexcept
    {
        switch (level)
        {
            case LogLevel::Trace:   return u"Trace";
            case LogLevel::Debug:   return u"Debug";
            case LogLevel::Info:    return u"Info";
            case LogLevel::Warning: return u"Warning";
            case LogLevel::Error:   return u"Error";
            case LogLevel::Fatal:   return u"Fatal";
            case LogLevel::Off:     return u"Off";
        }
        return u"?";
    }

    // -----------------------------------------------------------------------
    // Sinks
    // -----------------------------------------------------------------------
    class ILogSink
    {
    public:
        virtual ~ILogSink() = default;
        virtual void Write(LogLevel level, StringView category, StringView message) noexcept = 0;
    };

    namespace detail
    {
        inline void FormatLine(FormatBuffer& line, LogLevel level, StringView category, StringView message)
        {
            FormatTo(line, u"[{}] {}: ", LogLevelName(level), category);
            line.Append(message.Data(), message.Size());
            line.Append(u'\n');
        }
    }

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

        void Dispatch(LogLevel level, StringView category, StringView message) noexcept
        {
            ScopedLock lock(m_mutex);
            for (ILogSink* sink : m_sinks)
            {
                sink->Write(level, category, message);
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
    void Logf(LogLevel level, StringView category, FormatString<Args...> fmt, const Args&... args)
    {
        Logger& logger = GlobalLogger();
        if (!logger.IsEnabled(level))
        {
            return;
        }

        FormatBuffer buffer;
        FormatToV(buffer, fmt.data, args...);
        logger.Dispatch(level, category, buffer.View());
    }
}
