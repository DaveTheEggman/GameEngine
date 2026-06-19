// Raptor Core — :ring_log_sink partition
//
// RingLogSink: keeps the most recent records in a ring buffer (tools / in-app
// consoles). Fixed-size records; long category/message text is truncated.

module;
#include "Core/Prelude.h"

export module raptor.core:ring_log_sink;

import :base;
import :allocator;
import :string;
import :ring_buffer;
import :logger;

export namespace raptor::core
{
    // Keeps the most recent messages in a ring buffer (for tools / in-app
    // consoles). Fixed-size records; long category/message text is truncated.
    struct LogRecord
    {
        LogLevel level;
        widechar category[64];
        widechar message[192];
    };

    class RingLogSink final : public ILogSink
    {
    public:
        explicit RingLogSink(usize capacity, IAllocator& allocator = DefaultAllocator())
            : m_records(capacity, allocator) {}

        void Write(LogLevel level, WideStringView category, WideStringView message) noexcept override
        {
            LogRecord record{};
            record.level = level;
            CopyTruncated(record.category, sizeof(record.category) / sizeof(widechar), category);
            CopyTruncated(record.message, sizeof(record.message) / sizeof(widechar), message);

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
        static void CopyTruncated(widechar* dst, usize dstCount, WideStringView src) noexcept
        {
            const usize n = (src.Size() < dstCount - 1) ? src.Size() : dstCount - 1;
            MemCopy(dst, src.Data(), n * sizeof(widechar));
            dst[n] = u'\0';
        }

        RingBuffer<LogRecord> m_records;
    };
}
