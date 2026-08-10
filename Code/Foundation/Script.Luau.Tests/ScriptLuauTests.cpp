// The Luau backend against the shared conformance battery (the certification bar every
// backend meets) with the Luau dialect: metatable-OOP classes, our coroutine primitives
// (startCoroutine / waitSeconds / waitUntil), closures as delegates.
#include <doctest/doctest.h>

#include <chrono>

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

    constexpr StringView kOverload = u8R"lua(
local over = Overloads.new()
OP0 = over:ping()
OP1 = over:ping(5)
OC = over:combine(2, 3)
OCT = over:combineText("x", 7)
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

    // A reflected type with a Float3 property + a Float3-arg/return method, to exercise the native
    // vector fast path through properties and methods (not just Float3.new).
    class VecHolder : public Object
    {
        RTTI_OBJECT(VecHolder, Object)
    public:
        Float3 pos = Float3(0.0f, 0.0f, 0.0f);
        [[nodiscard]] Float3 scaledBy(f32 s) const
        {
            return Float3(pos.x * s, pos.y * s, pos.z * s);
        }
    };

    constexpr StringView kVectorProbe = u8R"lua(
VecProbe = {}
VecProbe.__index = VecProbe
function VecProbe.new() return setmetatable({ h = VecHolder.new() }, VecProbe) end
function VecProbe:ctorX() return Float3.new(2, 3, 4).x end
function VecProbe:ctorZ() return Float3.new(2, 3, 4).z end
function VecProbe:arith() local v = Float3.new(1, 2, 3); return (v + v).y end
function VecProbe:setGetZ()
    self.h.pos = Float3.new(5, 6, 7)
    return self.h.pos.z
end
function VecProbe:methodRet()
    self.h.pos = Float3.new(2, 2, 2)
    return self.h:scaledBy(3).x
end
function VecProbe:dot() return Float3.Dot(Float3.new(1, 2, 3), Float3.new(4, 5, 6)) end
)lua";
}

REFLECT_MEMBERS(VecHolder, "rtti::luau::test")
{
    builder.Constructor();
    builder.Property<&VecHolder::pos>("pos");
    builder.Method<&VecHolder::scaledBy>("scaledBy");
}

TEST_CASE("script.luau: Float3 maps to Luau's native vector (fast path - fields, arithmetic, marshalling)")
{
    RegisterCoreTypes(); // reflects Float3 (patches TypeOf<Float3>)

    RefPtr<IScriptManager> manager = CreateLuauScriptManager();
    manager->RegisterType(TypeOf<Float3>()); // the class table: Float3.new / Float3.Dot / ...
    manager->RegisterType(VecHolder::StaticType());
    manager->FinalizeTypes();

    RefPtr<IScriptContext> context = manager->CreateContext();
    REQUIRE(context->Load(kVectorProbe, u8"luau.vector").IsOk());
    RefPtr<ScriptObject> probe = context->CreateInstance(u8"VecProbe", Span<Variant>{});
    REQUIRE(probe.Get() != nullptr);

    // Float3.new returns a NATIVE vector: .x/.z are Luau's built-in fields, not a metatable.
    CHECK(probe->Invoke(u8"ctorX", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(2.0));
    CHECK(probe->Invoke(u8"ctorZ", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(4.0));
    // Native vector arithmetic (v + v).
    CHECK(probe->Invoke(u8"arith", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(4.0));
    // A Float3 PROPERTY round-trips as a vector (set from a vector, get returns one).
    CHECK(probe->Invoke(u8"setGetZ", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(7.0));
    // A Float3 method ARGUMENT (self) + Float3 RETURN marshal as vectors.
    CHECK(probe->Invoke(u8"methodRet", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(6.0));
    // A vector passed to a reflected static, number returned (1*4 + 2*5 + 3*6).
    CHECK(probe->Invoke(u8"dot", Span<Variant>{}).Value().Get<f64>() == doctest::Approx(32.0));
}

TEST_CASE("script.luau: native-vector construct+access throughput (perf case)")
{
    RegisterCoreTypes();
    RefPtr<IScriptManager> manager = CreateLuauScriptManager();
    manager->RegisterType(TypeOf<Float3>());
    manager->FinalizeTypes();
    RefPtr<IScriptContext> context = manager->CreateContext();
    // Hot loop: construct a Float3 and read all three fields each iteration - the exact case the
    // native-vector path optimizes (no per-value userdata alloc, native field reads). MEASURED
    // (RelWithDebInfo, 200k iters): ~33.9 ms native vector vs ~89.3 ms boxed - about 2.6x - and the
    // native path additionally ENABLES `v + v` vector arithmetic the boxed handle cannot do at all.
    REQUIRE(context
                ->Load(u8"function hot(n)\n"
                       u8"  local sum = 0.0\n"
                       u8"  for i = 1, n do\n"
                       u8"    local v = Float3.new(1, 2, 3)\n"
                       u8"    sum = sum + v.x + v.y + v.z\n"
                       u8"  end\n"
                       u8"  return sum\n"
                       u8"end\n",
                       u8"luau.perf")
                .IsOk());
    constexpr f64 kIterations = 200000.0;
    Variant args[] = {Variant::From<f64>(kIterations)};
    const auto start = std::chrono::steady_clock::now();
    Result<Variant> result = context->Call(u8"hot", Span<Variant>{args, 1});
    const auto finish = std::chrono::steady_clock::now();
    REQUIRE(result.HasValue());
    CHECK(result.Value().Get<f64>() == doctest::Approx(6.0 * kIterations)); // (1+2+3) per iteration
    const double ms = std::chrono::duration<double, std::milli>(finish - start).count();
    MESSAGE("Float3 construct + 3 field reads x " << static_cast<long>(kIterations) << ": " << ms
                                                  << " ms");
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
    dialect.overloadModule = kOverload;

    conformance::RunScriptBackendConformance([]() { return CreateLuauScriptManager(); },
                                             dialect);
}
