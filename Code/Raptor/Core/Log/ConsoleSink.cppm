// Raptor Core — :console_sink partition
//
// ConsoleSink: writes formatted lines to stdout (stderr for Error/Fatal).

module;
#include "Core/Prelude.h"

export module raptor.core:console_sink;

import :base;
import :format;
import :system;
import :logger;

export namespace raptor::core
{
    // Writes to stdout (stderr for Error/Fatal).
    class ConsoleSink final : public ILogSink
    {
    public:
        void Write(LogLevel level, StringView category, StringView message) noexcept override
        {
            FormatBuffer line;
            detail::FormatLine(line, level, category, message);

            if (static_cast<u8>(level) >= static_cast<u8>(LogLevel::Error))
            {
                ConsoleWriteError(line.View());
            }
            else
            {
                ConsoleWrite(line.View());
            }
        }
    };
}
