#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/RTTI/Reflect.h"

import raptor.core;

using namespace raptor::core;

namespace
{
    // Builds a formatted string and returns whether it equals `expected`.
    template <typename... Args>
    bool FormatEquals(const char* expected, const char* fmt, const Args&... args)
    {
        FormatBuffer buffer;
        FormatTo(buffer, fmt, args...);
        return std::strcmp(buffer.Data(), expected) == 0;
    }
}

// --- Format ----------------------------------------------------------------

TEST_CASE("format: substitution and types")
{
    CHECK(FormatEquals("no args", "no args"));
    CHECK(FormatEquals("a=1 b=2", "a={} b={}", 1, 2));
    CHECK(FormatEquals("neg -42", "neg {}", -42));
    CHECK(FormatEquals("big 4294967295", "big {}", 4294967295u));
    CHECK(FormatEquals("flag true and false", "flag {} and {}", true, false));
    CHECK(FormatEquals("char X", "char {}", 'X'));
    CHECK(FormatEquals("str hello", "str {}", "hello"));
    CHECK(FormatEquals("pi 3.5", "pi {}", 3.5));
}

TEST_CASE("format: brace escapes and extra/missing args")
{
    CHECK(FormatEquals("{literal}", "{{literal}}"));
    CHECK(FormatEquals("set {x} = 7", "set {{x}} = {}", 7));
    CHECK(FormatEquals("only 1", "only {}", 1, 2, 3)); // extra args ignored
    CHECK(FormatEquals("missing {}", "missing {}"));    // unmatched placeholder left as-is
}

// --- Log -------------------------------------------------------------------

namespace
{
    struct CapturingSink : ILogSink
    {
        int count = 0;
        LogLevel lastLevel = LogLevel::Off;
        char lastCategory[64] = {};
        char lastMessage[256] = {};

        void Write(LogLevel level, const char* category,
                   const char* message, usize length) noexcept override
        {
            ++count;
            lastLevel = level;
            std::strncpy(lastCategory, category, sizeof(lastCategory) - 1);
            const usize n = (length < sizeof(lastMessage) - 1) ? length : sizeof(lastMessage) - 1;
            std::memcpy(lastMessage, message, n);
            lastMessage[n] = '\0';
        }
    };
}

TEST_CASE("log: dispatch, formatting, and level filtering")
{
    CapturingSink sink;
    Logger& logger = GlobalLogger();
    const LogLevel previousLevel = logger.MinLevel();

    logger.AddSink(&sink);
    logger.SetMinLevel(LogLevel::Info);

    RAPTOR_LOG_INFO("Renderer", "loaded {} meshes", 12);
    CHECK(sink.count == 1);
    CHECK(sink.lastLevel == LogLevel::Info);
    CHECK(std::strcmp(sink.lastCategory, "Renderer") == 0);
    CHECK(std::strcmp(sink.lastMessage, "loaded 12 meshes") == 0);

    // Below the min level -> filtered out.
    RAPTOR_LOG_DEBUG("Renderer", "verbose {}", 1);
    CHECK(sink.count == 1);

    // At/above min level -> delivered.
    RAPTOR_LOG_ERROR("Audio", "device {} lost", 3);
    CHECK(sink.count == 2);
    CHECK(sink.lastLevel == LogLevel::Error);
    CHECK(std::strcmp(sink.lastMessage, "device 3 lost") == 0);

    logger.RemoveSink(&sink);
    logger.SetMinLevel(previousLevel);

    // After removal, no more delivery.
    RAPTOR_LOG_ERROR("Audio", "ignored");
    CHECK(sink.count == 2);
}
