#include <doctest/doctest.h>

#include "Core/Prelude.h"             // <new> reachability for reflection containers (GCC)
#include "Core/Reflection/Reflect.h"

import raptor.core;
import raptor.script;
import raptor.script.wren;

using namespace raptor::core;
using namespace raptor::script;

// A reflected Object-derived type to exercise object foreign classes in Wren.
namespace
{
    class Widget : public Object
    {
        RAPTOR_OBJECT(Widget, Object)
    public:
        int id = 0;
        int doubled() const { return id * 2; }
        int idOf(Widget* other) const { return other != nullptr ? other->id : -1; }
    };
}

RAPTOR_REFLECT(Widget, "raptor::script::test")
{
    builder.Property<&Widget::id>("id");
    builder.Method<&Widget::doubled>("doubled");
    builder.Method<&Widget::idOf>("idOf");
    builder.Constructor();
}

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

namespace
{
    struct CapturingErrors final : IScriptErrorHandler
    {
        int count = 0;
        ScriptErrorKind lastKind = ScriptErrorKind::Compile;
        String lastMessage;
        i32 lastLine = -1;

        void OnError(const ScriptError& error) override
        {
            ++count;
            lastKind = error.kind;
            lastMessage = String(error.message);
            lastLine = error.line;
        }
    };
}

TEST_CASE("wren: errors are surfaced to a handler")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    CapturingErrors errors;
    ctx->SetErrorHandler(&errors);

    // Compile error: kind + non-empty message + a source line.
    CHECK_FALSE(ctx->Load(u"var = = =", u"main").IsOk());
    CHECK(errors.count >= 1);
    CHECK(errors.lastKind == ScriptErrorKind::Compile);
    CHECK(!errors.lastMessage.IsEmpty());
    CHECK(errors.lastLine >= 1);

    // Runtime error: kind switches to Runtime.
    const int afterCompile = errors.count;
    CHECK_FALSE(ctx->Load(u"Fiber.abort(\"boom\")", u"main").IsOk());
    CHECK(errors.count > afterCompile);
    CHECK(errors.lastKind == ScriptErrorKind::Runtime);
    CHECK(!errors.lastMessage.IsEmpty());

    // Clearing the handler restores default (console) reporting — no more captures.
    ctx->SetErrorHandler(nullptr);
    const int afterRuntime = errors.count;
    CHECK_FALSE(ctx->Load(u"more @#$ garbage", u"main").IsOk());
    CHECK(errors.count == afterRuntime);
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

TEST_CASE("wren: read module globals as Variant")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    REQUIRE(ctx->Load(
        u"var Answer = 42\n"
        u"var Name = \"raptor\"\n"
        u"var Flag = true\n",
        u"main").IsOk());

    CHECK(ctx->GetGlobal(u"Answer").Get<f64>() == 42.0);   // Wren numbers are doubles
    CHECK(ctx->GetGlobal(u"Name").Get<String>() == u"raptor");
    CHECK(ctx->GetGlobal(u"Flag").Get<bool>() == true);

    CHECK(ctx->GetGlobal(u"Missing").IsEmpty());           // absent -> empty Variant
}

TEST_CASE("wren: reflected value types are usable from script (construct + properties)")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);                 // reflection -> manager
    RefPtr<IScriptContext> ctx = manager->CreateContext(); // emits Wren foreign classes

    // Construct a reflected Vec3 from Wren, read and write its properties.
    const Status status = ctx->Load(
        u"var v = Vec3.new(1, 2, 3)\n"
        u"var X = v.x\n"
        u"var Z = v.z\n"
        u"v.x = 9\n"
        u"var X2 = v.x\n",
        u"main");
    REQUIRE(status.IsOk());

    CHECK(ctx->GetGlobal(u"X").Get<f64>() == 1.0);    // construct + getter
    CHECK(ctx->GetGlobal(u"Z").Get<f64>() == 3.0);
    CHECK(ctx->GetGlobal(u"X2").Get<f64>() == 9.0);   // setter took effect
}

TEST_CASE("wren: call reflected methods (static, instance, struct return, foreign args)")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    // Static method with foreign args, scalar return: Vec3.Dot(a, b).
    REQUIRE(ctx->Load(
        u"var a = Vec3.new(1, 2, 3)\n"
        u"var b = Vec3.new(4, 5, 6)\n"
        u"var D = Vec3.Dot(a, b)\n",
        u"main").IsOk());
    CHECK(ctx->GetGlobal(u"D").Get<f64>() == 32.0);

    // Instance method returning a struct (Vec4.XYZ() -> Vec3), then read it.
    REQUIRE(ctx->Load(
        u"var v4 = Vec4.new(7, 8, 9, 10)\n"
        u"var xyz = v4.XYZ()\n"
        u"var XX = xyz.x\n"
        u"var ZZ = xyz.z\n",
        u"main").IsOk());
    CHECK(ctx->GetGlobal(u"XX").Get<f64>() == 7.0);
    CHECK(ctx->GetGlobal(u"ZZ").Get<f64>() == 9.0);

    // Instance method taking a foreign arg, returning bool; constructed from
    // foreign args too (AABB.new(Vec3, Vec3)).
    REQUIRE(ctx->Load(
        u"var box = AABB.new(Vec3.new(0, 0, 0), Vec3.new(10, 10, 10))\n"
        u"var inside = box.Contains(Vec3.new(5, 5, 5))\n"
        u"var outside = box.Contains(Vec3.new(20, 0, 0))\n",
        u"main").IsOk());
    CHECK(ctx->GetGlobal(u"inside").Get<bool>() == true);
    CHECK(ctx->GetGlobal(u"outside").Get<bool>() == false);
}

TEST_CASE("wren: same-name overloads resolve by argument type")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    // Vec3.Mul has two overloads: (Vec3, Vec3) componentwise and (Vec3, f32) scale.
    REQUIRE(ctx->Load(
        u"var p = Vec3.new(2, 3, 4)\n"
        u"var comp = Vec3.Mul(p, Vec3.new(1, 2, 3))\n"  // -> (2, 6, 12)
        u"var scaled = Vec3.Mul(p, 2)\n"                // -> (4, 6, 8)
        u"var CX = comp.x\n"
        u"var CZ = comp.z\n"
        u"var SX = scaled.x\n",
        u"main").IsOk());
    CHECK(ctx->GetGlobal(u"CX").Get<f64>() == 2.0);    // chose (Vec3, Vec3)
    CHECK(ctx->GetGlobal(u"CZ").Get<f64>() == 12.0);
    CHECK(ctx->GetGlobal(u"SX").Get<f64>() == 4.0);    // chose (Vec3, f32)
}

TEST_CASE("wren: Object-derived type as a foreign class")
{
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    manager->RegisterType(Widget::StaticType());       // register just the Object type
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    REQUIRE(ctx->Load(
        u"var w = Widget.new()\n"
        u"w.id = 21\n"
        u"var w2 = Widget.new()\n"
        u"w2.id = 5\n"
        u"var I = w.id\n"
        u"var D = w.doubled()\n"     // instance method -> 42
        u"var O = w.idOf(w2)\n",     // object argument -> 5
        u"main").IsOk());
    CHECK(ctx->GetGlobal(u"I").Get<f64>() == 21.0);
    CHECK(ctx->GetGlobal(u"D").Get<f64>() == 42.0);
    CHECK(ctx->GetGlobal(u"O").Get<f64>() == 5.0);
}

TEST_CASE("wren: a default-constructed reflected type")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    // Guid has a default ctor and (u64, u64); its props round-trip through doubles.
    REQUIRE(ctx->Load(
        u"var g = Guid.new(7, 42)\n"
        u"var Hi = g.high\n"
        u"var Lo = g.low\n",
        u"main").IsOk());
    CHECK(ctx->GetGlobal(u"Hi").Get<f64>() == 7.0);
    CHECK(ctx->GetGlobal(u"Lo").Get<f64>() == 42.0);
}

TEST_CASE("wren: call a script function with marshalled args")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    REQUIRE(ctx->Load(
        u"var add = Fn.new { |a, b| a + b }\n"
        u"var greeting = Fn.new { \"hi\" }\n",
        u"main").IsOk());

    CHECK(ctx->HasFunction(u"add"));
    CHECK_FALSE(ctx->HasFunction(u"nope"));

    // int args marshal to Wren numbers; result comes back as a double.
    Variant addArgs[] = { Variant::From(2), Variant::From(3) };
    CHECK(ctx->Call(u"add", Span<Variant>{ addArgs, 2 }).Value().Get<f64>() == 5.0);

    // no-arg call returning a string.
    CHECK(ctx->Call(u"greeting", Span<Variant>{}).Value().Get<String>() == u"hi");

    // missing callable -> NotFound.
    CHECK(ctx->Call(u"nope", Span<Variant>{}).Error() == ErrorCode::NotFound);
}
