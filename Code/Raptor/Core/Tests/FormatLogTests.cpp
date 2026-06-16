#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;

using namespace raptor::core;

namespace
{
    // Builds a formatted string and returns whether it equals `expected`.
    template <typename... Args>
    bool FormatEquals(const char* expected, const char* fmt, const Args&... args)
    {
        FormatBuffer buffer;
        FormatToV(buffer, fmt, args...);
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

// --- Log: thread safety ----------------------------------------------------

namespace
{
    struct CountingSink : ILogSink
    {
        Atomic<int> count{ 0 };
        void Write(LogLevel, const char*, const char*, usize) noexcept override
        {
            count.fetch_add(1);
        }
    };
}

TEST_CASE("log: concurrent logging is serialized by the logger")
{
    Logger& logger = GlobalLogger();
    const LogLevel previous = logger.MinLevel();
    logger.SetMinLevel(LogLevel::Info);

    CountingSink sink;
    logger.AddSink(&sink);

    constexpr int kThreads = 4;
    constexpr int kPerThread = 500;

    Array<Thread> threads;
    for (int i = 0; i < kThreads; ++i)
    {
        threads.PushBack(Thread([]() {
            for (int j = 0; j < kPerThread; ++j) { RAPTOR_LOG_INFO("Worker", "tick {}", j); }
        }));
    }
    for (Thread& t : threads) { t.Join(); }

    logger.RemoveSink(&sink);
    logger.SetMinLevel(previous);

    CHECK(sink.count.load() == kThreads * kPerThread);
}

// --- Format: wide / UTF-8 string arguments ---------------------------------

TEST_CASE("format: wide and utf8 string arguments")
{
    // Wide String / StringView transcode to UTF-8 in the output.
    String wide = u"café";
    CHECK(FormatEquals("name=café", "name={}", wide));
    CHECK(FormatEquals("v=héllo", "v={}", StringView(u"héllo")));

    // UTF-8 view appends directly.
    CHECK(FormatEquals("u=café", "u={}", UTF8StringView(u8"café")));
}

// --- Log: in-memory ring sink ----------------------------------------------

TEST_CASE("log: RingLogSink keeps the most recent records")
{
    RingLogSink ring(3);
    CHECK(ring.Count() == 0u);

    for (int i = 0; i < 5; ++i)
    {
        char msg[16];
        msg[0] = 'm';
        msg[1] = static_cast<char>('0' + i);
        msg[2] = '\0';
        ring.Write(LogLevel::Info, "Cat", msg, 2);
    }

    // Capacity 3 -> keeps the last three (m2, m3, m4).
    CHECK(ring.Count() == 3u);
    CHECK(std::strcmp(ring.Record(0).message, "m2") == 0);
    CHECK(std::strcmp(ring.Record(2).message, "m4") == 0);
    CHECK(ring.Record(0).level == LogLevel::Info);
    CHECK(std::strcmp(ring.Record(0).category, "Cat") == 0);
}

TEST_CASE("format: checked FormatTo validates arg count at compile time")
{
    // Correct placeholder/arg count compiles and formats as usual.
    FormatBuffer buffer;
    FormatTo(buffer, "a={} b={}", 1, 2);
    CHECK(std::strcmp(buffer.Data(), "a=1 b=2") == 0);

    // A mismatched count, e.g. FormatTo(buffer, "x={}", 1, 2), would fail to
    // compile via FormatString's consteval constructor.
}
