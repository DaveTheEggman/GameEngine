#include <doctest/doctest.h>

import raptor.core;
import raptor.script;
import raptor.script.wren;

using namespace raptor::core;
using namespace raptor::script;

TEST_CASE("wren: a context runs valid source")
{
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    REQUIRE(static_cast<bool>(manager));

    RefPtr<IScriptContext> ctx = manager->CreateContext();
    REQUIRE(static_cast<bool>(ctx));

    // A real class-based Wren program: define a class, instantiate, call a method.
    const Status status = ctx->Load(
        u"class Greeter {\n"
        u"  construct new(name) { _name = name }\n"
        u"  greet() { System.print(\"hi %(_name)\") }\n"
        u"}\n"
        u"Greeter.new(\"raptor\").greet()\n",
        u"main");
    CHECK(status.IsOk());
}

TEST_CASE("wren: a compile error is reported")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    const Status status = ctx->Load(u"this is not valid wren @#$", u"main");
    CHECK_FALSE(status.IsOk());
    CHECK(status.Code() == ErrorCode::InvalidArgument);
}

TEST_CASE("wren: a runtime error is reported")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    const Status status = ctx->Load(u"Fiber.abort(\"boom\")", u"main");
    CHECK_FALSE(status.IsOk());
    CHECK(status.Code() == ErrorCode::Internal);
}

TEST_CASE("wren: each context is an isolated VM")
{
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RefPtr<IScriptContext> a = manager->CreateContext();
    RefPtr<IScriptContext> b = manager->CreateContext();
    CHECK(a.Get() != b.Get());
    CHECK(a->Load(u"var X = 1", u"main").IsOk());
    CHECK(b->Load(u"var Y = 2", u"main").IsOk());
}
