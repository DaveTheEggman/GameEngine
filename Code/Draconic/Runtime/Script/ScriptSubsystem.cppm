// Draconic::RuntimeScript - the `draconic.runtime.script` module.
//
// ScriptSubsystem: hosts scripting inside the runtime. It owns a script manager
// (the VM backend is injected - e.g. Wren - so the runtime stays backend-
// agnostic) and its main context, exposes reflected engine types to scripts,
// loads script source, and instantiates script objects.
//
// A single "driver" script object can be set to receive the frame loop - tier 1
// of the scripting design: a global script that drives the game (no states/
// stages). Tier 2 (per-entity script components ticked by an ECS) reuses the
// same CreateInstance/Invoke primitives once an ECS exists, so it needs no new
// machinery here.

module;
#include "Core/Prelude.h"

export module draconic.runtime.script;

import draconic.core;
import draconic.runtime;
import draconic.script;

namespace core = draconic::core;
namespace script = draconic::script;

export namespace draconic::runtime
{
    class ScriptSubsystem final : public Subsystem
    {
    public:
        explicit ScriptSubsystem(core::RefPtr<script::IScriptManager> manager,
                                 const core::TypeRegistry& registry = core::GlobalTypeRegistry()) noexcept
            : m_manager(core::Move(manager)), m_registry(&registry) {}

        [[nodiscard]] script::IScriptManager* Manager() noexcept { return m_manager.Get(); }
        [[nodiscard]] script::IScriptContext* Context() noexcept { return m_context.Get(); }

        // Compile and run script source in the main context. Defaults to the
        // "main" module so the reflected foreign classes are in scope.
        core::Status Load(core::StringView source, core::StringView chunkName = u8"main")
        {
            return (m_context.Get() != nullptr) ? m_context->Load(source, chunkName)
                                          : core::Status{ core::ErrorCode::Internal };
        }

        // Instantiate a script-defined class (see IScriptContext::CreateInstance).
        [[nodiscard]] core::RefPtr<script::ScriptObject> CreateInstance(
            core::StringView className, core::Span<core::Variant> args)
        {
            return (m_context.Get() != nullptr) ? m_context->CreateInstance(className, args)
                                          : core::RefPtr<script::ScriptObject>{};
        }

        // Set (or clear, with null) the global driver object. While set, it
        // receives update(dt) each frame. The subsystem holds a strong reference.
        void SetDriver(core::RefPtr<script::ScriptObject> driver) noexcept { m_driver = core::Move(driver); }
        [[nodiscard]] script::ScriptObject* Driver() noexcept { return m_driver.Get(); }

        // Forward the per-frame update to the driver script (if any).
        void Update(core::f32 deltaTime) override
        {
            if (m_driver.Get() != nullptr)
            {
                core::Variant args[] = { core::Variant::From<core::f32>(deltaTime) };
                (void)m_driver->Invoke(u8"update", core::Span<core::Variant>{ args, 1 });
            }
        }

    protected:
        // Expose the reflected types, then create the main context (which snapshots
        // the types registered up to that point).
        void OnInit() override
        {
            if (m_manager.Get() == nullptr) { return; }
            script::RegisterReflectedTypes(*m_manager, *m_registry);
            m_context = m_manager->CreateContext();
        }

        void OnShutdown() override
        {
            m_driver.Reset();
            m_context.Reset();
        }

    private:
        core::RefPtr<script::IScriptManager> m_manager;
        const core::TypeRegistry* m_registry;
        core::RefPtr<script::IScriptContext> m_context;
        core::RefPtr<script::ScriptObject> m_driver;
    };
}
