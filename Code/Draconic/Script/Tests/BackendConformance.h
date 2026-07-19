// The script-backend CONFORMANCE BATTERY (scripting.md B2): every registered backend
// must pass this against its own language dialect - a backend is DONE when this is
// green, never "hopefully it works". Include from a doctest TU and call
// RunScriptBackendConformance inside a TEST_CASE.
//
// The battery certifies the CONTEXT contract: manager/context lifecycle, the two-phase
// type registration flow, load/compile/runtime error reporting through the handler,
// Variant marshalling both ways, script-class instantiation + method dispatch, context
// isolation, services, and GC hooks. Reflected-type EMISSION (foreign classes for
// engine types) is certified separately per backend by porting the Wren reflected-type
// suite - class syntax differs too much per language to share source.
#pragma once

#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.script;

namespace draconic::script::conformance
{
    using namespace draconic::core;

    /// The language-specific sources. Every snippet implements a FIXED contract:
    ///  - functionsModule: function `add(a, b)` returning a + b, function `greeting()`
    ///    returning the string "hi", a module global `answer` readable as 42.
    ///  - counterClass: class `Counter`, constructed with one number, method
    ///    `increment()` adding 1, zero-arg method `value()` returning the count.
    ///  - compileBroken: source that CANNOT compile.
    ///  - runtimeFault: source that compiles but faults at load/run time.
    struct Dialect
    {
        StringView languageId;
        StringView functionsModule;
        StringView counterClass;
        StringView compileBroken;
        StringView runtimeFault;
    };

    struct CapturedErrors final : IScriptErrorHandler
    {
        int count = 0;
        ScriptErrorKind lastKind = ScriptErrorKind::Compile;
        String lastMessage;
        void OnError(const ScriptError& error) override
        {
            ++count;
            lastKind = error.kind;
            lastMessage = String(error.message);
        }
    };

    inline void RunScriptBackendConformance(const Function<RefPtr<IScriptManager>()>& factory,
                                            const Dialect& dialect)
    {
        // --- manager + two-phase type registration (collect, then finalize) ---
        RefPtr<IScriptManager> manager = factory();
        REQUIRE(manager.Get() != nullptr);
        RegisterReflectedTypes(*manager);   // walks the registry + FinalizeTypes()

        // --- context + valid load ---
        RefPtr<IScriptContext> context = manager->CreateContext();
        REQUIRE(context.Get() != nullptr);
        CHECK(context->Load(dialect.functionsModule, u8"conformance.functions").IsOk());

        // --- Variant marshalling: numbers in/out, strings out, missing = NotFound ---
        {
            Variant args[] = { Variant::From(2), Variant::From(3) };
            auto sum = context->Call(u8"add", Span<Variant>{ args, 2 });
            REQUIRE(sum.HasValue());
            CHECK(sum.Value().Get<f64>() == doctest::Approx(5.0));
        }
        {
            auto text = context->Call(u8"greeting", Span<Variant>{});
            REQUIRE(text.HasValue());
            CHECK(text.Value().Get<String>() == u8"hi");
        }
        CHECK(context->Call(u8"no_such_function", Span<Variant>{}).Error()
              == ErrorCode::NotFound);

        // --- module globals surface as Variants ---
        CHECK(context->GetGlobal(u8"answer").Get<f64>() == doctest::Approx(42.0));

        // --- script classes: construct with args, invoke methods, state persists.
        // NOTE the contract does NOT promise a later Load preserves an earlier load's
        // globals (Wren replaces the module) - only that live ScriptObjects survive. ---
        CHECK(context->Load(dialect.counterClass, u8"conformance.counter").IsOk());
        RefPtr<ScriptObject> counter;
        {
            Variant ctorArgs[] = { Variant::From(10) };
            counter = context->CreateInstance(u8"Counter", Span<Variant>{ ctorArgs, 1 });
            REQUIRE(counter.Get() != nullptr);
            CHECK(counter->Invoke(u8"increment", Span<Variant>{}).HasValue());
            auto value = counter->Invoke(u8"value", Span<Variant>{});
            REQUIRE(value.HasValue());
            CHECK(value.Value().Get<f64>() == doctest::Approx(11.0));
        }

        // --- error reporting: compile + runtime kinds through the handler ---
        {
            RefPtr<IScriptContext> errorContext = manager->CreateContext();
            CapturedErrors errors;
            errorContext->SetErrorHandler(&errors);
            CHECK_FALSE(errorContext->Load(dialect.compileBroken, u8"conformance.bad").IsOk());
            CHECK(errors.count >= 1);
            CHECK(errors.lastKind == ScriptErrorKind::Compile);
            CHECK(!errors.lastMessage.IsEmpty());
            const int afterCompile = errors.count;
            CHECK_FALSE(errorContext->Load(dialect.runtimeFault, u8"conformance.fault").IsOk());
            CHECK(errors.count > afterCompile);
            CHECK(errors.lastKind == ScriptErrorKind::Runtime);
        }

        // --- context isolation: a second context sees none of the first's state ---
        {
            RefPtr<IScriptContext> other = manager->CreateContext();
            REQUIRE(other.Get() != nullptr);
            CHECK_FALSE(other->HasFunction(u8"add"));
        }

        // --- per-context services: pointer round-trip, unknown = null ---
        {
            int payload = 7;
            context->SetService(u8"conformance.service", &payload);
            CHECK(context->GetService(u8"conformance.service") == &payload);
            CHECK(context->GetService(u8"conformance.unknown") == nullptr);
            context->SetService(u8"conformance.service", nullptr);
            CHECK(context->GetService(u8"conformance.service") == nullptr);
        }

        // --- GC hook: callable any time; live script objects and their state survive ---
        manager->CollectGarbage();
        {
            auto value = counter->Invoke(u8"value", Span<Variant>{});
            REQUIRE(value.HasValue());
            CHECK(value.Value().Get<f64>() == doctest::Approx(11.0));
        }
    }
}
