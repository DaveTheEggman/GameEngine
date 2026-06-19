#include <doctest/doctest.h>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"

import raptor.core;

using namespace raptor::core;

namespace
{
    // Builds a formatted string and returns whether it equals `expected`.
    template <typename... Args>
    bool FormatEquals(const widechar* expected, const widechar* fmt, const Args&... args)
    {
        FormatBuffer buffer;
        FormatToV(buffer, fmt, args...);
        return buffer.View() == WideStringView(expected);
    }
}

// --- Format ----------------------------------------------------------------

TEST_CASE("format: substitution and types")
{
    CHECK(FormatEquals(u"no args", u"no args"));
    CHECK(FormatEquals(u"a=1 b=2", u"a={} b={}", 1, 2));
    CHECK(FormatEquals(u"neg -42", u"neg {}", -42));
    CHECK(FormatEquals(u"big 4294967295", u"big {}", 4294967295u));
    CHECK(FormatEquals(u"flag true and false", u"flag {} and {}", true, false));
    CHECK(FormatEquals(u"char X", u"char {}", 'X'));
    CHECK(FormatEquals(u"str hello", u"str {}", u"hello"));
    CHECK(FormatEquals(u"pi 3.5", u"pi {}", 3.5));
}

TEST_CASE("format: brace escapes and extra/missing args")
{
    CHECK(FormatEquals(u"{literal}", u"{{literal}}"));
    CHECK(FormatEquals(u"set {x} = 7", u"set {{x}} = {}", 7));
    CHECK(FormatEquals(u"only 1", u"only {}", 1, 2, 3)); // extra args ignored
    CHECK(FormatEquals(u"missing {}", u"missing {}"));    // unmatched placeholder left as-is
}

// --- Log -------------------------------------------------------------------

namespace
{
    struct CapturingSink : ILogSink
    {
        int count = 0;
        LogLevel lastLevel = LogLevel::Off;
        String lastCategory;
        String lastMessage;

        void Write(LogLevel level, StringView category, StringView message) noexcept override
        {
            ++count;
            lastLevel = level;
            lastCategory = String(category);
            lastMessage = String(message);
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

    RAPTOR_LOG_INFO(u8"Renderer", u"loaded {} meshes", 12);
    CHECK(sink.count == 1);
    CHECK(sink.lastLevel == LogLevel::Info);
    CHECK(sink.lastCategory == u8"Renderer");
    CHECK(sink.lastMessage == u8"loaded 12 meshes");

    // Below the min level -> filtered out.
    RAPTOR_LOG_DEBUG(u8"Renderer", u"verbose {}", 1);
    CHECK(sink.count == 1);

    // At/above min level -> delivered.
    RAPTOR_LOG_ERROR(u8"Audio", u"device {} lost", 3);
    CHECK(sink.count == 2);
    CHECK(sink.lastLevel == LogLevel::Error);
    CHECK(sink.lastMessage == u8"device 3 lost");

    logger.RemoveSink(&sink);
    logger.SetMinLevel(previousLevel);

    // After removal, no more delivery.
    RAPTOR_LOG_ERROR(u8"Audio", u"ignored");
    CHECK(sink.count == 2);
}

// --- Log: thread safety ----------------------------------------------------

namespace
{
    struct CountingSink : ILogSink
    {
        Atomic<int> count{ 0 };
        void Write(LogLevel, StringView, StringView) noexcept override
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
            for (int j = 0; j < kPerThread; ++j) { RAPTOR_LOG_INFO(u8"Worker", u"tick {}", j); }
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
    // Wide WideString / WideStringView append directly.
    WideString wide = u"caf\u00e9";
    CHECK(FormatEquals(u"name=caf\u00e9", u"name={}", wide));
    CHECK(FormatEquals(u"v=h\u00e9llo", u"v={}", WideStringView(u"h\u00e9llo")));

    // UTF-8 view transcodes to wide.
    CHECK(FormatEquals(u"u=caf\u00e9", u"u={}", StringView(u8"caf\u00e9")));
}

// --- Log: in-memory ring sink ----------------------------------------------

TEST_CASE("log: RingLogSink keeps the most recent records")
{
    RingLogSink ring(3);
    CHECK(ring.Count() == 0u);

    for (int i = 0; i < 5; ++i)
    {
        utf8char msg[3];
        msg[0] = utf8char('m');
        msg[1] = static_cast<utf8char>(utf8char('0') + i);
        msg[2] = utf8char('\0');
        ring.Write(LogLevel::Info, u8"Cat", StringView(msg, 2));
    }

    // Capacity 3 -> keeps the last three (m2, m3, m4).
    CHECK(ring.Count() == 3u);
    CHECK(StringView(ring.Record(0).message) == StringView(u8"m2"));
    CHECK(StringView(ring.Record(2).message) == StringView(u8"m4"));
    CHECK(ring.Record(0).level == LogLevel::Info);
    CHECK(StringView(ring.Record(0).category) == StringView(u8"Cat"));
}

TEST_CASE("format: checked FormatTo validates arg count at compile time")
{
    // Correct placeholder/arg count compiles and formats as usual.
    FormatBuffer buffer;
    FormatTo(buffer, u"a={} b={}", 1, 2);
    CHECK(buffer.View() == WideStringView(u"a=1 b=2"));

    // A mismatched count, e.g. FormatTo(buffer, u"x={}", 1, 2), would fail to
    // compile via FormatString's consteval constructor.
}
