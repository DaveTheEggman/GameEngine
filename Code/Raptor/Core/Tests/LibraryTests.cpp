#include <doctest/doctest.h>

#include <cstring>

#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"
#include "Core/RTTI/Reflect.h"

import raptor.core;

using namespace raptor::core;

// --- Library ---------------------------------------------------------------

TEST_CASE("library: load a real plugin, resolve and call symbols, unload")
{
    DynamicLibrary lib;
    CHECK_FALSE(lib.IsLoaded());

    REQUIRE(lib.Load(RAPTOR_TEST_PLUGIN_PATH).IsOk());
    CHECK(lib.IsLoaded());

    using AddFn = int (*)(int, int);
    AddFn add = lib.GetSymbol<AddFn>("RaptorTestAdd");
    REQUIRE(add != nullptr);
    CHECK(add(2, 3) == 5);

    using AnswerFn = int (*)();
    AnswerFn answer = lib.GetSymbol<AnswerFn>("RaptorTestAnswer");
    REQUIRE(answer != nullptr);
    CHECK(answer() == 42);

    CHECK(lib.GetSymbol<AddFn>("NoSuchSymbol") == nullptr);

    lib.Unload();
    CHECK_FALSE(lib.IsLoaded());
}

TEST_CASE("library: loading a missing file fails cleanly")
{
    DynamicLibrary lib;
    Status status = lib.Load("raptor_definitely_not_a_library.so");
    CHECK_FALSE(status.IsOk());
    CHECK(status.Code() == ErrorCode::NotFound);
    CHECK_FALSE(lib.IsLoaded());
}

TEST_CASE("library: move transfers ownership")
{
    DynamicLibrary a;
    REQUIRE(a.Load(RAPTOR_TEST_PLUGIN_PATH).IsOk());

    DynamicLibrary b = Move(a);
    CHECK_FALSE(a.IsLoaded());
    CHECK(b.IsLoaded());

    using AnswerFn = int (*)();
    AnswerFn answer = b.GetSymbol<AnswerFn>("RaptorTestAnswer");
    REQUIRE(answer != nullptr);
    CHECK(answer() == 42);
}
