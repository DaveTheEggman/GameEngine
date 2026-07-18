// Draconic::InputSubsystem - the `draconic.input.subsystem` module.
//
// The runtime hookup: owns the ActionRuntime + the device provider and evaluates once per
// frame in Update (before scenes tick - Subsystem registration order puts input ahead of
// the scene subsystem in every assembly that adds it first). The provider seam is the
// play-in-editor story: the player passes the shell's devices, the editor's Game tab will
// pass its viewport's gated InputSurface facades.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

export module draconic.input.subsystem;

import draconic.core;
import draconic.shell;
import draconic.runtime;
import draconic.script;
import draconic.input;

using namespace draconic::core;

export namespace draconic::input
{
    /// The service key ExposeToScript binds and the scripting facade resolves.
    inline constexpr StringView kInputRuntimeService = u8"input.runtime";

    class InputSubsystem final : public draconic::runtime::Subsystem
    {
    public:
        /// `input` = the shell's device hub (null tolerated: headless runs read released).
        explicit InputSubsystem(draconic::shell::IInputManager* input)
            : m_shellSource(input) {}

        [[nodiscard]] ActionRuntime& Runtime() noexcept { return m_runtime; }

        /// Installs (copies) a map - from the cooked resource, a test, or hand-authored.
        void SetMap(const InputMap& map) { m_runtime.SetMap(map); }

        /// Overrides the device source (play-in-editor: the Game viewport's gated facades).
        /// Null restores the shell devices.
        void SetSourceProvider(IInputSourceProvider* provider) noexcept { m_override = provider; }

        /// The device source actions currently evaluate against - the UI subsystem reads
        /// the SAME facades (so game UI sees viewport-transformed coordinates in the Game
        /// tab and window coordinates in the player, transparently).
        [[nodiscard]] IInputSourceProvider& ActiveSource() noexcept
        {
            return (m_override != nullptr) ? *m_override
                                           : static_cast<IInputSourceProvider&>(m_shellSource);
        }

        /// Binds THIS subsystem's runtime as `context`'s input service - the scripting
        /// facade (class Input below) resolves it per context, so two contexts can read
        /// two different runtimes (players; editor vs game). Call once per created context.
        void ExposeToScript(draconic::script::IScriptContext& context)
        {
            context.SetService(kInputRuntimeService, &m_runtime);
        }

        void Update(f32 deltaTime) override
        {
            if (draconic::runtime::Context* context = GetContext())
            {
                m_runtime.SetTimeScale(context->TimeScale());
            }
            IInputSourceProvider& devices =
                (m_override != nullptr) ? *m_override
                                        : static_cast<IInputSourceProvider&>(m_shellSource);
            m_runtime.Update(devices, deltaTime);
        }

    private:
        ShellInputSource m_shellSource;
        IInputSourceProvider* m_override = nullptr;   // borrowed
        ActionRuntime m_runtime;
    };

    // The scripting facade: a foreign class named `Input` whose STATIC methods resolve the
    // CURRENT script context's bound runtime (draconic.script's CurrentScriptContext seam,
    // pushed by the backend around every reflected dispatch). NO process globals: a context
    // without the service - or a call from outside any script - reads released. The Wren
    // backend cannot inject host objects as module globals (wren has no host-side variable
    // set), which is why the API is statics-on-a-foreign-class rather than a passed object.
    class Input final : public Object
    {
        DRACONIC_OBJECT(Input, Object)
    public:
        [[nodiscard]] static ActionRuntime* Resolve()
        {
            draconic::script::IScriptContext* context = draconic::script::CurrentScriptContext();
            return context != nullptr
                ? static_cast<ActionRuntime*>(context->GetService(kInputRuntimeService))
                : nullptr;
        }

        [[nodiscard]] static bool isDown(String name)
        {
            ActionRuntime* rt = Resolve();
            return rt != nullptr && rt->IsDown(rt->Resolve(name.AsView()));
        }
        [[nodiscard]] static bool wasPressed(String name)
        {
            ActionRuntime* rt = Resolve();
            return rt != nullptr && rt->WasPressed(rt->Resolve(name.AsView()));
        }
        [[nodiscard]] static bool wasReleased(String name)
        {
            ActionRuntime* rt = Resolve();
            return rt != nullptr && rt->WasReleased(rt->Resolve(name.AsView()));
        }
        [[nodiscard]] static f32 value(String name)
        {
            ActionRuntime* rt = Resolve();
            return rt != nullptr ? rt->Value(rt->Resolve(name.AsView())) : 0.0f;
        }
        [[nodiscard]] static f32 valueX(String name) { return value(static_cast<String&&>(name)); }
        [[nodiscard]] static f32 valueY(String name)
        {
            ActionRuntime* rt = Resolve();
            return rt != nullptr ? rt->Value2D(rt->Resolve(name.AsView())).y : 0.0f;
        }
        static void pushSet(String name)
        {
            if (ActionRuntime* rt = Resolve()) { rt->PushExclusiveSet(name.AsView()); }
        }
        static void popSet()
        {
            if (ActionRuntime* rt = Resolve()) { rt->PopExclusiveSet(); }
        }
        static void enableSet(String name, bool enabled)
        {
            if (ActionRuntime* rt = Resolve()) { rt->EnableSet(name.AsView(), enabled); }
        }
    };

    /// Registers the facade into the global type registry (RegisterReflectedTypes then
    /// sweeps it into any script manager).
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
        // The Wren emitter only materializes CONSTRUCTIBLE types as foreign classes.
        builder.Constructor();
    }

    void RegisterInputScriptApi()
    {
        GlobalTypeRegistry().Register(Input::StaticType());
    }
}
