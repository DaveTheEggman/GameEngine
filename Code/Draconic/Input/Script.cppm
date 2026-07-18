// Draconic::Input - :script partition.
//
// The scripting facade (input P2): a reflected class named `Input` whose STATIC methods
// read the process-bound ActionRuntime. Scripts see a foreign class - Input.isDown("Jump")
// - via the ordinary reflection->script bridge; no script-library dependency lands here.
//
// WHY statics + a bound pointer: the Wren backend cannot inject host objects as module
// globals (wren has no host-side variable set), so a singleton-instance API cannot reach
// scripts - a foreign class with statics is the supported shape. The input runtime is
// engine-global per player BY DESIGN, so the binding matches the model; PlayerInput (P3)
// revisits when several runtimes exist.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.input:script;

import draconic.core;
import :model;
import :runtime;

using namespace draconic::core;

export namespace draconic::input
{
    class Input final : public Object
    {
        DRACONIC_OBJECT(Input, Object)
    public:
        /// Bound by the InputSubsystem (null = every query reads released).
        static void BindRuntime(ActionRuntime* runtime) noexcept { s_runtime = runtime; }
        [[nodiscard]] static ActionRuntime* BoundRuntime() noexcept { return s_runtime; }

        [[nodiscard]] static bool isDown(String name)
        {
            return s_runtime != nullptr && s_runtime->IsDown(s_runtime->Resolve(name.AsView()));
        }
        [[nodiscard]] static bool wasPressed(String name)
        {
            return s_runtime != nullptr && s_runtime->WasPressed(s_runtime->Resolve(name.AsView()));
        }
        [[nodiscard]] static bool wasReleased(String name)
        {
            return s_runtime != nullptr && s_runtime->WasReleased(s_runtime->Resolve(name.AsView()));
        }
        [[nodiscard]] static f32 value(String name)
        {
            return s_runtime != nullptr ? s_runtime->Value(s_runtime->Resolve(name.AsView())) : 0.0f;
        }
        [[nodiscard]] static f32 valueX(String name) { return value(static_cast<String&&>(name)); }
        [[nodiscard]] static f32 valueY(String name)
        {
            return s_runtime != nullptr
                ? s_runtime->Value2D(s_runtime->Resolve(name.AsView())).y : 0.0f;
        }
        static void pushSet(String name)
        {
            if (s_runtime != nullptr) { s_runtime->PushExclusiveSet(name.AsView()); }
        }
        static void popSet()
        {
            if (s_runtime != nullptr) { s_runtime->PopExclusiveSet(); }
        }
        static void enableSet(String name, bool enabled)
        {
            if (s_runtime != nullptr) { s_runtime->EnableSet(name.AsView(), enabled); }
        }

    private:
        static inline ActionRuntime* s_runtime = nullptr;
    };

    /// Registers the scripting facade into the global type registry (so
    /// RegisterReflectedTypes sweeps it into any script manager). Idempotent-safe to
    /// call alongside the other input registrations.
    void RegisterInputScriptApi();
}

namespace draconic::input
{
    DRACONIC_REFLECT(Input, "draconic::input")
    {
        builder.Method<&Input::isDown>("isDown");
        builder.Method<&Input::wasPressed>("wasPressed");
        builder.Method<&Input::wasReleased>("wasReleased");
        builder.Method<&Input::value>("value");
        builder.Method<&Input::valueX>("valueX");
        builder.Method<&Input::valueY>("valueY");
        builder.Method<&Input::pushSet>("pushSet");
        builder.Method<&Input::popSet>("popSet");
        builder.Method<&Input::enableSet>("enableSet");
        // The Wren emitter only materializes CONSTRUCTIBLE types as foreign classes -
        // the statics ride on the class object, but the class must exist to hold them.
        builder.Constructor();
    }

    void RegisterInputScriptApi()
    {
        GlobalTypeRegistry().Register(Input::StaticType());
    }
}
