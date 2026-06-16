// Raptor Core — :file_sink partition
//
// FileSink: appends formatted lines to a file.

module;
#include "Core/Prelude.h"

export module raptor.core:file_sink;

import :base;
import :format;
import :string;
import :system;
import :logger;

export namespace raptor::core
{
    // Appends to a file.
    class FileSink final : public ILogSink
    {
    public:
        explicit FileSink(StringView path) noexcept { m_file = FileOpen(path, FileMode::Append); }
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
}
