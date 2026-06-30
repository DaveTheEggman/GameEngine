#include <doctest/doctest.h>

#include "Core/Prelude.h"             // <new> reachability for reflection containers (GCC)
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.script;
import draconic.script.wren;

using namespace draconic::core;
using namespace draconic::script;

// A reflected Object-derived type to exercise object foreign classes in Wren.
namespace
{
    class Widget : public Object
    {
        DRACONIC_OBJECT(Widget, Object)
    public:
        int id = 0;
        int doubled() const { return id * 2; }
        int idOf(Widget* other) const { return other != nullptr ? other->id : -1; }
    };
}

DRACONIC_REFLECT(Widget, "draconic::script::test")
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
        u8"class Greeter {\n"
        u8"  construct new(name) { _name = name }\n"
        u8"  greet() { System.print(\"hi %(_name)\") }\n"
        u8"}\n"
        u8"Greeter.new(\"draconic\").greet()\n",
        u8"main");
    CHECK(status.IsOk());
}

TEST_CASE("wren: a compile error is reported")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    const Status status = ctx->Load(u8"this is not valid wren @#$", u8"main");
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
    CHECK_FALSE(ctx->Load(u8"var = = =", u8"main").IsOk());
    CHECK(errors.count >= 1);
    CHECK(errors.lastKind == ScriptErrorKind::Compile);
    CHECK(!errors.lastMessage.IsEmpty());
    CHECK(errors.lastLine >= 1);

    // Runtime error: kind switches to Runtime.
    const int afterCompile = errors.count;
    CHECK_FALSE(ctx->Load(u8"Fiber.abort(\"boom\")", u8"main").IsOk());
    CHECK(errors.count > afterCompile);
    CHECK(errors.lastKind == ScriptErrorKind::Runtime);
    CHECK(!errors.lastMessage.IsEmpty());

    // Clearing the handler restores default (console) reporting — no more captures.
    ctx->SetErrorHandler(nullptr);
    const int afterRuntime = errors.count;
    CHECK_FALSE(ctx->Load(u8"more @#$ garbage", u8"main").IsOk());
    CHECK(errors.count == afterRuntime);
}

TEST_CASE("wren: a runtime error is reported")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    const Status status = ctx->Load(u8"Fiber.abort(\"boom\")", u8"main");
    CHECK_FALSE(status.IsOk());
    CHECK(status.Code() == ErrorCode::Internal);
}

TEST_CASE("wren: each context is an isolated VM")
{
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RefPtr<IScriptContext> a = manager->CreateContext();
    RefPtr<IScriptContext> b = manager->CreateContext();
    CHECK(a.Get() != b.Get());
    CHECK(a->Load(u8"var X = 1", u8"main").IsOk());
    CHECK(b->Load(u8"var Y = 2", u8"main").IsOk());
}

TEST_CASE("wren: read module globals as Variant")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    REQUIRE(ctx->Load(
        u8"var Answer = 42\n"
        u8"var Name = \"draconic\"\n"
        u8"var Flag = true\n",
        u8"main").IsOk());

    CHECK(ctx->GetGlobal(u8"Answer").Get<f64>() == 42.0);   // Wren numbers are doubles
    CHECK(ctx->GetGlobal(u8"Name").Get<String>() == u8"draconic");
    CHECK(ctx->GetGlobal(u8"Flag").Get<bool>() == true);

    CHECK(ctx->GetGlobal(u8"Missing").IsEmpty());           // absent -> empty Variant
}

TEST_CASE("wren: reflected value types are usable from script (construct + properties)")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);                 // reflection -> manager
    RefPtr<IScriptContext> ctx = manager->CreateContext(); // emits Wren foreign classes

    // Construct a reflected Vec3 from Wren, read and write its properties.
    const Status status = ctx->Load(
        u8"var v = Vec3.new(1, 2, 3)\n"
        u8"var X = v.x\n"
        u8"var Z = v.z\n"
        u8"v.x = 9\n"
        u8"var X2 = v.x\n",
        u8"main");
    REQUIRE(status.IsOk());

    CHECK(ctx->GetGlobal(u8"X").Get<f64>() == 1.0);    // construct + getter
    CHECK(ctx->GetGlobal(u8"Z").Get<f64>() == 3.0);
    CHECK(ctx->GetGlobal(u8"X2").Get<f64>() == 9.0);   // setter took effect
}

TEST_CASE("wren: call reflected methods (static, instance, struct return, foreign args)")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    // Static method with foreign args, scalar return: Vec3.Dot(a, b).
    REQUIRE(ctx->Load(
        u8"var a = Vec3.new(1, 2, 3)\n"
        u8"var b = Vec3.new(4, 5, 6)\n"
        u8"var D = Vec3.Dot(a, b)\n",
        u8"main").IsOk());
    CHECK(ctx->GetGlobal(u8"D").Get<f64>() == 32.0);

    // Instance method returning a struct (Vec4.XYZ() -> Vec3), then read it.
    REQUIRE(ctx->Load(
        u8"var v4 = Vec4.new(7, 8, 9, 10)\n"
        u8"var xyz = v4.XYZ()\n"
        u8"var XX = xyz.x\n"
        u8"var ZZ = xyz.z\n",
        u8"main").IsOk());
    CHECK(ctx->GetGlobal(u8"XX").Get<f64>() == 7.0);
    CHECK(ctx->GetGlobal(u8"ZZ").Get<f64>() == 9.0);

    // Instance method taking a foreign arg, returning bool; constructed from
    // foreign args too (AABB.new(Vec3, Vec3)).
    REQUIRE(ctx->Load(
        u8"var box = AABB.new(Vec3.new(0, 0, 0), Vec3.new(10, 10, 10))\n"
        u8"var inside = box.Contains(Vec3.new(5, 5, 5))\n"
        u8"var outside = box.Contains(Vec3.new(20, 0, 0))\n",
        u8"main").IsOk());
    CHECK(ctx->GetGlobal(u8"inside").Get<bool>() == true);
    CHECK(ctx->GetGlobal(u8"outside").Get<bool>() == false);
}

TEST_CASE("wren: same-name overloads resolve by argument type")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    // Vec3.Mul has two overloads: (Vec3, Vec3) componentwise and (Vec3, f32) scale.
    REQUIRE(ctx->Load(
        u8"var p = Vec3.new(2, 3, 4)\n"
        u8"var comp = Vec3.Mul(p, Vec3.new(1, 2, 3))\n"  // -> (2, 6, 12)
        u8"var scaled = Vec3.Mul(p, 2)\n"                // -> (4, 6, 8)
        u8"var CX = comp.x\n"
        u8"var CZ = comp.z\n"
        u8"var SX = scaled.x\n",
        u8"main").IsOk());
    CHECK(ctx->GetGlobal(u8"CX").Get<f64>() == 2.0);    // chose (Vec3, Vec3)
    CHECK(ctx->GetGlobal(u8"CZ").Get<f64>() == 12.0);
    CHECK(ctx->GetGlobal(u8"SX").Get<f64>() == 4.0);    // chose (Vec3, f32)
}

TEST_CASE("wren: Object-derived type as a foreign class")
{
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    manager->RegisterType(Widget::StaticType());       // register just the Object type
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    REQUIRE(ctx->Load(
        u8"var w = Widget.new()\n"
        u8"w.id = 21\n"
        u8"var w2 = Widget.new()\n"
        u8"w2.id = 5\n"
        u8"var I = w.id\n"
        u8"var D = w.doubled()\n"     // instance method -> 42
        u8"var O = w.idOf(w2)\n",     // object argument -> 5
        u8"main").IsOk());
    CHECK(ctx->GetGlobal(u8"I").Get<f64>() == 21.0);
    CHECK(ctx->GetGlobal(u8"D").Get<f64>() == 42.0);
    CHECK(ctx->GetGlobal(u8"O").Get<f64>() == 5.0);
}

TEST_CASE("wren: a default-constructed reflected type")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = wren::CreateScriptManager();
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    // Guid has a default ctor and (u64, u64); its props round-trip through doubles.
    REQUIRE(ctx->Load(
        u8"var g = Guid.new(7, 42)\n"
        u8"var Hi = g.high\n"
        u8"var Lo = g.low\n",
        u8"main").IsOk());
    CHECK(ctx->GetGlobal(u8"Hi").Get<f64>() == 7.0);
    CHECK(ctx->GetGlobal(u8"Lo").Get<f64>() == 42.0);
}

TEST_CASE("wren: call a script function with marshalled args")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    REQUIRE(ctx->Load(
        u8"var add = Fn.new { |a, b| a + b }\n"
        u8"var greeting = Fn.new { \"hi\" }\n",
        u8"main").IsOk());

    CHECK(ctx->HasFunction(u8"add"));
    CHECK_FALSE(ctx->HasFunction(u8"nope"));

    // int args marshal to Wren numbers; result comes back as a double.
    Variant addArgs[] = { Variant::From(2), Variant::From(3) };
    CHECK(ctx->Call(u8"add", Span<Variant>{ addArgs, 2 }).Value().Get<f64>() == 5.0);

    // no-arg call returning a string.
    CHECK(ctx->Call(u8"greeting", Span<Variant>{}).Value().Get<String>() == u8"hi");

    // missing callable -> NotFound.
    CHECK(ctx->Call(u8"nope", Span<Variant>{}).Error() == ErrorCode::NotFound);
}

TEST_CASE("wren: instantiate a script class and invoke its methods")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    REQUIRE(ctx->Load(
        u8"class Counter {\n"
        u8"  construct new(start) { _n = start }\n"
        u8"  add(x) { _n = _n + x }\n"
        u8"  value() { _n }\n"    // a zero-arg method (Invoke models methods, not getters)
        u8"  reset() { _n = 0 }\n"
        u8"}\n",
        u8"main").IsOk());

    Variant ctorArgs[] = { Variant::From(10) };
    RefPtr<ScriptObject> counter = ctx->CreateInstance(u8"Counter", Span<Variant>{ ctorArgs, 1 });
    REQUIRE(static_cast<bool>(counter));

    Variant addArgs[] = { Variant::From(5) };
    CHECK(counter->Invoke(u8"add", Span<Variant>{ addArgs, 1 }).HasValue());

    // Zero-arg getter: signature has no parens, so call it by its bare name.
    CHECK(counter->Invoke(u8"value", Span<Variant>{}).Value().Get<f64>() == 15.0);

    CHECK(counter->Invoke(u8"reset", Span<Variant>{}).HasValue());
    CHECK(counter->Invoke(u8"value", Span<Variant>{}).Value().Get<f64>() == 0.0);
}

TEST_CASE("wren: CreateInstance returns null for an unknown class")
{
    RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
    REQUIRE(ctx->Load(u8"class Known { construct new() {} }\n", u8"main").IsOk());
    CHECK_FALSE(static_cast<bool>(ctx->CreateInstance(u8"Missing", Span<Variant>{})));
}

TEST_CASE("wren: a script object outlives the local context reference")
{
    RefPtr<ScriptObject> obj;
    {
        RefPtr<IScriptContext> ctx = wren::CreateScriptManager()->CreateContext();
        REQUIRE(ctx->Load(
            u8"class Echo {\n"
            u8"  construct new() {}\n"
            u8"  ping() { 42 }\n"
            u8"}\n", u8"main").IsOk());
        obj = ctx->CreateInstance(u8"Echo", Span<Variant>{});
        REQUIRE(static_cast<bool>(obj));
        // ctx goes out of scope here; obj retains it (keeps the VM alive).
    }
    CHECK(obj->Invoke(u8"ping", Span<Variant>{}).Value().Get<f64>() == 42.0);
}
