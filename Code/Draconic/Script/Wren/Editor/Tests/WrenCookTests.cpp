// draconic.script.wren.editor tests: the Wren cook service in isolation - the starter
// template, the `static properties` harvest, and the Wren-only `is Behavior` coroutine
// opt-in - driven straight through IScriptLanguageCook (no project/VFS needed since Cook
// takes source bytes directly). The full source->builder->factory round-trip is covered
// by the neutral pipeline suite (which links this cook).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.script;
import draconic.script.resource;
import draconic.script.editor;
import draconic.script.wren.editor;

using namespace draconic::core;
using namespace draconic::script;

namespace
{
    [[nodiscard]] IScriptLanguageCook* WrenCook()
    {
        RegisterWrenScriptCook();   // registers the Wren backend + cook (idempotent)
        return ScriptLanguageCookRegistry::Get().FindByLanguage(u8"wren");
    }
}

TEST_CASE("wren.cook: registered + resolvable by language; the starter cooks with its "
          "declared property + handlers")
{
    IScriptLanguageCook* cook = WrenCook();
    REQUIRE(cook != nullptr);
    CHECK_FALSE(cook->NewAssetTemplate().IsEmpty());

    CookScriptErrorSink sink;
    ScriptClassSource out;
    REQUIRE(cook->Cook(cook->NewAssetTemplate(), u8"NewBehavior.wren", sink, out));
    CHECK(out.language == u8"wren");
    CHECK(out.className == u8"NewBehavior");
    REQUIRE(out.properties.Size() == 1u);
    CHECK(out.properties[0].name == u8"speed");
    CHECK(out.properties[0].type == ScriptPropertyType::Float);
    CHECK(out.handlers.Size() == 3u);   // onStart, onUpdate, onDestroy
}

TEST_CASE("wren.cook: `is Behavior` flags usesCoroutines (the Wren-only opt-in lives here)")
{
    IScriptLanguageCook* cook = WrenCook();
    REQUIRE(cook != nullptr);
    CookScriptErrorSink sink;
    ScriptClassSource out;
    REQUIRE(cook->Cook(
        u8"class Waiter is Behavior {\n"
        u8"    construct new(entity) { super(entity) }\n"
        u8"    onStart() {}\n"
        u8"}\n",
        u8"waiter.wren", sink, out));
    CHECK(out.usesCoroutines);
}

TEST_CASE("wren.cook: a compile error FAILS the cook (last good record untouched)")
{
    IScriptLanguageCook* cook = WrenCook();
    REQUIRE(cook != nullptr);
    CookScriptErrorSink sink;
    ScriptClassSource out;
    CHECK_FALSE(cook->Cook(u8"class Mover {\n  this is not wren at all(\n",
                           u8"broken.wren", sink, out));
}

TEST_CASE("wren.cook: the New-Asset starter template compiles clean (example calls resolve)")
{
    IScriptLanguageCook* cook = WrenCook();
    REQUIRE(cook != nullptr);
    CookScriptErrorSink sink;
    ScriptClassSource out;
    const bool ok = cook->Cook(cook->NewAssetTemplate(), u8"NewBehavior.wren", sink, out);
    REQUIRE(ok);   // the starter MUST compile - it teaches the API by example
    CHECK(out.className == u8"NewBehavior");
}
