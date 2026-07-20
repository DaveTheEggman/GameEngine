// draconic.script.angelscript.editor tests: the MINIMAL AngelScript cook - compile-check
// in a cooker-owned AngelScript VM (resolved by language), the SHARED handler scan, the
// starter template, and the documented DEFERRAL of property harvest (no metadata yet).

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import draconic.core;
import draconic.script;
import draconic.script.resource;
import draconic.script.editor;
import draconic.script.angelscript.editor;

using namespace draconic::core;
using namespace draconic::script;

namespace
{
    [[nodiscard]] IScriptLanguageCook* AngelScriptCook()
    {
        RegisterAngelScriptScriptCook();   // registers the AS backend + cook (idempotent)
        return ScriptLanguageCookRegistry::Get().FindByLanguage(u8"angelscript");
    }
}

TEST_CASE("as.cook: registered + resolvable by language; supplies a starter template")
{
    IScriptLanguageCook* cook = AngelScriptCook();
    REQUIRE(cook != nullptr);
    CHECK_FALSE(cook->NewAssetTemplate().IsEmpty());
}

TEST_CASE("as.cook: an AngelScript behavior compile-checks, scans its on<Upper>(...) "
          "handlers, and cooks with NO property metadata (harvest deferred)")
{
    IScriptLanguageCook* cook = AngelScriptCook();
    REQUIRE(cook != nullptr);

    CookScriptErrorSink sink;
    ScriptClassSource out;
    const bool ok = cook->Cook(
        u8"class Bouncer\n"
        u8"{\n"
        u8"    private Entity@ self;\n"
        u8"    Bouncer(Entity@ e) { @self = e; }\n"
        u8"    void onStart() {}\n"
        u8"    void onUpdate(double dt) {}\n"
        u8"    void onHeal(double amount) {}\n"
        u8"}\n",
        u8"bouncer.as", sink, out);
    REQUIRE(ok);
    CHECK(out.language == u8"angelscript");
    CHECK(out.className == u8"Bouncer");
    auto scanned = [&out](StringView name) {
        for (const String& h : out.handlers) { if (h.AsView() == name) { return true; } }
        return false;
    };
    CHECK(scanned(u8"onStart"));
    CHECK(scanned(u8"onUpdate"));
    CHECK(scanned(u8"onHeal"));
    CHECK(out.handlers.Size() == 3u);
    CHECK(out.properties.IsEmpty());             // property harvest is DEFERRED for AS
}

TEST_CASE("as.cook: the starter template itself compiles clean")
{
    IScriptLanguageCook* cook = AngelScriptCook();
    REQUIRE(cook != nullptr);
    CookScriptErrorSink sink;
    ScriptClassSource out;
    CHECK(cook->Cook(cook->NewAssetTemplate(), u8"NewBehavior.as", sink, out));
    CHECK(out.className == u8"NewBehavior");
}

TEST_CASE("as.cook: a compile error FAILS the cook (the last good record is untouched)")
{
    IScriptLanguageCook* cook = AngelScriptCook();
    REQUIRE(cook != nullptr);
    CookScriptErrorSink sink;
    ScriptClassSource out;
    CHECK_FALSE(cook->Cook(u8"class Broken { void foo( }\n", u8"broken.as", sink, out));
}
