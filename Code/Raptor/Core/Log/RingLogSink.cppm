// Raptor Core — :ring_log_sink partition
//
// RingLogSink: keeps the most recent records in a ring buffer (tools / in-app
// consoles). Fixed-size records; long category/message text is truncated.

module;
#include "Core/Prelude.h"

export module raptor.core:ring_log_sink;

import :base;
import :memory;
import :ring_buffer;
import :log;

export namespace raptor::core
{
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
}
