// Integration.Script - JSON through the scripting backends.
//
// SUBJECT is the cross-collection flow foundation.json x foundation.script.<backend>: a script
// parses, queries, builds, and stringifies JSON through the reflected value type - the user
// requirement that made JsonValue RTTI-reflected. This needs the script VMs (a "weird dependency"
// for the JSON module), so it lives here, not in Json.Tests (which stays pure DOM). See
// Code/Integration/README.md.

#include <doctest/doctest.h>

#include <cstring>
#include <initializer_list>

#include "Core/Prelude.h"

import foundation.core;
import foundation.json;
import foundation.script;
import foundation.script.wren;
#if INTEGRATION_HAS_ANGELSCRIPT
import foundation.script.angelscript;
#endif

using namespace foundation::core;
using namespace foundation::json;
using namespace foundation::script;

// The reflected surface scripts consume (a precursor sanity check before driving a VM).
TEST_CASE("integration.script: JsonValue's reflected surface is registered")
{
    RegisterJsonTypes();
    const TypeInfo& t = TypeOf<JsonValue>();
    CHECK(std::strcmp(t.namespaceName, "rtti::foundation::json") == 0);
    for (const char* m : {"MakeObject", "MakeNumber", "MakeBool", "Set", "Get", "AsNumber",
                          "AsString", "IsObject", "ToString", "Parse"})
    {
        CHECK_MESSAGE(FindMethod(t, m) != nullptr, m);
    }
}

TEST_CASE("integration.script: parse / query / build / stringify from Wren")
{
    RegisterJsonTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    const Status status =
        ctx->Load(u8"var doc = JsonValue.Parse(\"{\\\"n\\\":7,\\\"name\\\":\\\"tool\\\"}\")\n"
                  u8"var N = doc.Get(\"n\").AsNumber(0)\n"
                  u8"var NAME = doc.Get(\"name\").AsString()\n"
                  u8"var IS_OBJ = doc.IsObject()\n"
                  u8"var o = JsonValue.MakeObject()\n"
                  u8"o.Set(\"id\", JsonValue.MakeNumber(1))\n"
                  u8"o.Set(\"ok\", JsonValue.MakeBool(true))\n"
                  u8"var OUT = o.ToString(false)\n",
                  u8"main");
    REQUIRE(status.IsOk());

    CHECK(ctx->GetGlobal(u8"N").Get<f64>() == 7.0);              // parse + typed accessor
    CHECK(ctx->GetGlobal(u8"NAME").Get<String>() == StringView(u8"tool"));
    CHECK(ctx->GetGlobal(u8"IS_OBJ").Get<bool>() == true);
    CHECK(ctx->GetGlobal(u8"OUT").Get<String>() == StringView(u8"{\"id\":1,\"ok\":true}")); // build
}

#if INTEGRATION_HAS_ANGELSCRIPT
TEST_CASE("integration.script: parse / query / build / stringify from AngelScript")
{
    RegisterJsonTypes();
    RefPtr<IScriptManager> manager = angelscript::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    const Status status =
        ctx->Load(u8"double N = 0;\n"
                  u8"string NAME;\n"
                  u8"bool ISOBJ = false;\n"
                  u8"string OUT;\n"
                  u8"void main() {\n"
                  u8"  JsonValue@ doc = JsonValue::Parse(\"{\\\"n\\\":7,\\\"name\\\":\\\"tool\\\"}\");\n"
                  u8"  N = doc.Get(\"n\").AsNumber(0);\n"
                  u8"  NAME = doc.Get(\"name\").AsString();\n"
                  u8"  ISOBJ = doc.IsObject();\n"
                  u8"  JsonValue@ o = JsonValue::MakeObject();\n"
                  u8"  o.Set(\"id\", JsonValue::MakeNumber(1));\n"
                  u8"  o.Set(\"ok\", JsonValue::MakeBool(true));\n"
                  u8"  OUT = o.ToString(false);\n"
                  u8"}\n",
                  u8"main");
    REQUIRE(status.IsOk());

    CHECK(ctx->GetGlobal(u8"N").Get<f64>() == 7.0);
    CHECK(ctx->GetGlobal(u8"NAME").Get<String>() == StringView(u8"tool"));
    CHECK(ctx->GetGlobal(u8"ISOBJ").Get<bool>() == true);
    CHECK(ctx->GetGlobal(u8"OUT").Get<String>() == StringView(u8"{\"id\":1,\"ok\":true}"));
}
#endif
