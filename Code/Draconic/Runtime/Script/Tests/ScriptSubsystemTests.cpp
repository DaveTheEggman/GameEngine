#include <doctest/doctest.h>

#include "Core/Prelude.h"  // <new> reachability for container instantiation (GCC)

import draconic.core;
import draconic.runtime;
import draconic.runtime.script;
import draconic.script;
import draconic.script.wren;

using namespace draconic::core;
using namespace draconic::runtime;
using namespace draconic::script;

namespace
{
    // A driver class that accumulates the per-frame dt and reports the total.
    constexpr StringView kDriverSource =
        u8"class Game {\n"
        u8"  construct new() { _total = 0 }\n"
        u8"  update(dt) { _total = _total + dt }\n"
        u8"  total() { _total }\n"
        u8"}\n";
}

TEST_CASE("runtime.script: subsystem hosts a context after startup")
{
    Context ctx;
    ScriptSubsystem* scripts = ctx.AddSubsystem<ScriptSubsystem>(wren::CreateScriptManager());
    CHECK(scripts->Context() == nullptr);  // not created until Init

    ctx.Startup();
    CHECK(scripts->Manager() != nullptr);
    CHECK(scripts->Context() != nullptr);
    CHECK(scripts->Load(kDriverSource).IsOk());
}

TEST_CASE("runtime.script: a driver script object receives the frame update")
{
    Context ctx;
    ScriptSubsystem* scripts = ctx.AddSubsystem<ScriptSubsystem>(wren::CreateScriptManager());
    ctx.Startup();
    REQUIRE(scripts->Load(kDriverSource).IsOk());

    RefPtr<ScriptObject> game = scripts->CreateInstance(u8"Game", Span<Variant>{});
    REQUIRE(static_cast<bool>(game));
    scripts->SetDriver(game);
    CHECK(scripts->Driver() == game.Get());

    // Driving the context forwards Update(dt) to the driver's update(dt).
    ctx.Update(0.5f);
    ctx.Update(0.25f);
    CHECK(game->Invoke(u8"total", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(0.75));
}

TEST_CASE("runtime.script: no driver set -> update is a harmless no-op")
{
    Context ctx;
    ctx.AddSubsystem<ScriptSubsystem>(wren::CreateScriptManager());
    ctx.Startup();
    ctx.Update(0.016f);  // must not crash with no driver
    ctx.Shutdown();
    CHECK_FALSE(ctx.IsRunning());
}
