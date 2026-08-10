// The Luau backend against the shared conformance battery (the certification bar every
// backend meets) with the Luau dialect: metatable-OOP classes, our coroutine primitives
// (startCoroutine / waitSeconds / waitUntil), closures as delegates.
#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include "Script.Tests/BackendConformance.h"

import foundation.core;
import foundation.script;
import foundation.script.luau;

using namespace foundation::core;
using namespace foundation::script;

namespace
{
    constexpr StringView kFunctions = u8R"lua(
function add(a, b)
    return a + b
end
function greeting()
    return "hi"
end
answer = 42
)lua";

    constexpr StringView kCounter = u8R"lua(
Counter = {}
Counter.__index = Counter
function Counter.new(start)
    return setmetatable({ count = start }, Counter)
end
function Counter:increment()
    self.count = self.count + 1
end
function Counter:value()
    return self.count
end
)lua";

    constexpr StringView kCompileBroken = u8R"lua(
function (
)lua";

    constexpr StringView kRuntimeFault = u8R"lua(
error("conformance fault")
)lua";

    constexpr StringView kCoroutine = u8R"lua(
Coro = {}
Coro.__index = Coro
function Coro.new()
    return setmetatable({ p = 0, flag = false }, Coro)
end
function Coro:begin()
    local this = self
    startCoroutine(function()
        waitSeconds(1.0)
        this.p = 1
    end, self)
end
function Coro:beginUntil()
    local this = self
    startCoroutine(function()
        waitUntil(function()
            return this.flag
        end)
        this.p = 1
    end, self)
end
function Coro:flip()
    self.flag = true
end
function Coro:progress()
    return self.p
end
)lua";

    constexpr StringView kDelegate = u8R"lua(
signal = DelegateSignal.new()
signal:Connect(function(v)
    return v * 2
end)
)lua";

    // --- enum fixture: a reflected type with an enum property + an enum-arg/return method ------
    enum class Facing : i32
    {
        North = 0,
        East = 1,
        South = 2,
        West = 5 // non-contiguous, so a bare-ordinal assumption would fail
    };

    class Compass : public Object
    {
        RTTI_OBJECT(Compass, Object)
    public:
        Facing facing = Facing::North;
        [[nodiscard]] Facing opposite(Facing f) const
        {
            switch (f)
            {
            case Facing::North:
                return Facing::South;
            case Facing::South:
                return Facing::North;
            case Facing::East:
                return Facing::West;
            case Facing::West:
                return Facing::East;
            }
            return Facing::North;
        }
    };

    // A script reads/writes the enum property by the named table and calls the enum method.
    constexpr StringView kEnumProbe = u8R"lua(
Probe = {}
Probe.__index = Probe
function Probe.new()
    return setmetatable({ c = Compass.new() }, Probe)
end
function Probe:readDefault() return self.c.facing end
function Probe:westConst() return Facing.West end
function Probe:setAndRead()
    self.c.facing = Facing.West
    return self.c.facing
end
function Probe:oppositeOfWest()
    return self.c:opposite(Facing.West)
end
)lua";
}

namespace
{
    // Overload fixtures: an ARITY FAMILY (ping / ping(x)) + a same-arity TYPE overload split by a
    // distinct overloadedName (combine / combineText).
    class Overloads : public Object
    {
        RTTI_OBJECT(Overloads, Object)
    public:
        [[nodiscard]] f64 ping() const { return 1.0; }
        [[nodiscard]] f64 ping(f64 x) const { return x + 100.0; }
        [[nodiscard]] f64 combine(f64 a, f64 b) const { return a + b; }
        [[nodiscard]] f64 combine(String, f64 b) const { return b; }
    };

    // A type that VIOLATES the contract: two methods on the same (name, arity, static) with no
    // distinct overloadedName. Registered nowhere (only the pure collision check runs on it).
    class Colliding : public Object
    {
        RTTI_OBJECT(Colliding, Object)
    public:
        [[nodiscard]] f64 clash(f64 a) const { return a; }
        [[nodiscard]] f64 clash(String) const { return 0.0; }
    };

    constexpr StringView kOverloadProbe = u8R"lua(
Probe2 = {}
Probe2.__index = Probe2
function Probe2.new() return setmetatable({ o = Overloads.new() }, Probe2) end
function Probe2:ping0() return self.o:ping() end
function Probe2:ping1() return self.o:ping(5) end
function Probe2:comb() return self.o:combine(2, 3) end
function Probe2:combText() return self.o:combineText("x", 7) end
)lua";
}

REFLECT_MEMBERS(Overloads, "rtti::luau::test")
{
    builder.Constructor();
    builder.Method<static_cast<f64 (Overloads::*)() const>(&Overloads::ping)>("ping");
    builder.Method<static_cast<f64 (Overloads::*)(f64) const>(&Overloads::ping)>("ping");
    builder.Method<static_cast<f64 (Overloads::*)(f64, f64) const>(&Overloads::combine)>("combine");
    builder.Method<static_cast<f64 (Overloads::*)(String, f64) const>(&Overloads::combine)>("combine")
        .OverloadedName("combineText");
}

REFLECT_MEMBERS(Colliding, "rtti::luau::test")
{
    builder.Constructor();
    builder.Method<static_cast<f64 (Colliding::*)(f64) const>(&Colliding::clash)>("clash");
    builder.Method<static_cast<f64 (Colliding::*)(String) const>(&Colliding::clash)>("clash");
}

TEST_CASE("script.luau: arity-family dispatch + same-arity distinct overloadedName")
{
    RefPtr<IScriptManager> manager = CreateLuauScriptManager();
    manager->RegisterType(Overloads::StaticType());
    manager->FinalizeTypes(); // must NOT trip - ping is an arity family, combine/combineText distinct

    RefPtr<IScriptContext> context = manager->CreateContext();
    REQUIRE(context->Load(kOverloadProbe, u8"luau.overload").IsOk());
    RefPtr<ScriptObject> probe = context->CreateInstance(u8"Probe2", Span<Variant>{});
    REQUIRE(probe.Get() != nullptr);

    CHECK(probe->Invoke(u8"ping0", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(1.0));
    CHECK(probe->Invoke(u8"ping1", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(105.0));
    CHECK(probe->Invoke(u8"comb", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(5.0));
    CHECK(probe->Invoke(u8"combText", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(7.0));
}

TEST_CASE("script.luau: the overload validator - arity family legal, same-arity dup a collision")
{
    // A clean type (arity family + distinct names) reports NO collision.
    CHECK(FindScriptMethodNameCollision(Overloads::StaticType()) == nullptr);
    // A same-(name, arity, static) pair without distinct overloadedNames IS a collision.
    CHECK(FindScriptMethodNameCollision(Colliding::StaticType()) != nullptr);
}

REFLECT_ENUM(Facing, "rtti::luau::test")
{
    builder.Value("North", Facing::North);
    builder.Value("East", Facing::East);
    builder.Value("South", Facing::South);
    builder.Value("West", Facing::West);
}

REFLECT_MEMBERS(Compass, "rtti::luau::test")
{
    builder.Constructor();
    builder.Property<&Compass::facing>("facing");
    builder.Method<&Compass::opposite>("opposite");
}

TEST_CASE("script.luau: reflected enums - named tables + number marshalling both ways")
{
    RttiRegisterEnum_Facing();

    RefPtr<IScriptManager> manager = CreateLuauScriptManager();
    manager->RegisterType(TypeOf<Facing>());
    manager->RegisterType(Compass::StaticType());
    manager->FinalizeTypes();

    RefPtr<IScriptContext> context = manager->CreateContext();
    REQUIRE(context->Load(kEnumProbe, u8"luau.enum").IsOk());
    RefPtr<ScriptObject> probe = context->CreateInstance(u8"Probe", Span<Variant>{});
    REQUIRE(probe.Get() != nullptr);

    // An enum property reads as its underlying number (North = 0).
    CHECK(probe->Invoke(u8"readDefault", Span<Variant>{}).Value().Get<f64>() ==
          doctest::Approx(0.0));
    // The named table exposes each value (non-contiguous West = 5).
    CHECK(probe->Invoke(u8"westConst", Span<Variant>{}).Value().Get<f64>() ==
          doctest::Approx(5.0));
    // Setting the property via the named table round-trips back through the getter.
    CHECK(probe->Invoke(u8"setAndRead", Span<Variant>{}).Value().Get<f64>() ==
          doctest::Approx(5.0));
    // An enum argument AND an enum return marshal through a method (opposite(West) = East = 1).
    CHECK(probe->Invoke(u8"oppositeOfWest", Span<Variant>{}).Value().Get<f64>() ==
          doctest::Approx(1.0));
}

TEST_CASE("script.luau: backend conformance battery")
{
    conformance::Dialect dialect;
    dialect.languageId = u8"luau";
    dialect.functionsModule = kFunctions;
    dialect.counterClass = kCounter;
    dialect.compileBroken = kCompileBroken;
    dialect.runtimeFault = kRuntimeFault;
    dialect.coroutineClass = kCoroutine;
    dialect.delegateModule = kDelegate;

    conformance::RunScriptBackendConformance([]() { return CreateLuauScriptManager(); },
                                             dialect);
}
