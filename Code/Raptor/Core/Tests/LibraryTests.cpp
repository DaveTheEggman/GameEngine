#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"

import raptor.core;

using namespace raptor::core;

// The plugin path is injected as a narrow build-system literal; the library API
// is wide, so transcode at the boundary (mirrors real call sites loading a path
// that originated as char* from a config/build tool).
static WideString WidePath(const char* p)
{
    return ToWide(StringView{ reinterpret_cast<const utf8char*>(p) });
}

// --- Library ---------------------------------------------------------------

TEST_CASE("library: load a real plugin, resolve and call symbols, unload")
{
    DynamicLibrary lib;
    CHECK_FALSE(lib.IsLoaded());

    REQUIRE(lib.Load(WidePath(RAPTOR_TEST_PLUGIN_PATH).AsView()).IsOk());
    CHECK(lib.IsLoaded());

    using AddFn = int (*)(int, int);
    AddFn add = lib.GetSymbol<AddFn>(u"RaptorTestAdd");
    REQUIRE(add != nullptr);
    CHECK(add(2, 3) == 5);

    using AnswerFn = int (*)();
    AnswerFn answer = lib.GetSymbol<AnswerFn>(u"RaptorTestAnswer");
    REQUIRE(answer != nullptr);
    CHECK(answer() == 42);

    CHECK(lib.GetSymbol<AddFn>(u"NoSuchSymbol") == nullptr);

    lib.Unload();
    CHECK_FALSE(lib.IsLoaded());
}

TEST_CASE("library: loading a missing file fails cleanly")
{
    DynamicLibrary lib;
    Status status = lib.Load(u"raptor_definitely_not_a_library.so");
    CHECK_FALSE(status.IsOk());
    CHECK(status.Code() == ErrorCode::NotFound);
    CHECK_FALSE(lib.IsLoaded());
}

TEST_CASE("library: move transfers ownership")
{
    DynamicLibrary a;
    REQUIRE(a.Load(WidePath(RAPTOR_TEST_PLUGIN_PATH).AsView()).IsOk());

    DynamicLibrary b = Move(a);
    CHECK_FALSE(a.IsLoaded());
    CHECK(b.IsLoaded());

    using AnswerFn = int (*)();
    AnswerFn answer = b.GetSymbol<AnswerFn>(u"RaptorTestAnswer");
    REQUIRE(answer != nullptr);
    CHECK(answer() == 42);
}
