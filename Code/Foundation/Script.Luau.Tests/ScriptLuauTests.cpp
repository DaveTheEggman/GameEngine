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
