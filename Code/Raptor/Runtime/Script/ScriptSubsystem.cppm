// Raptor::RuntimeScript — the `raptor.runtime.script` module.
//
// ScriptSubsystem: hosts scripting inside the runtime. It owns a script manager
// (the VM backend is injected — e.g. Wren — so the runtime stays backend-
// agnostic) and its main context, exposes reflected engine types to scripts,
// loads script source, and instantiates script objects.
//
// A single "driver" script object can be set to receive the frame loop — tier 1
// of the scripting design: a global script that drives the game (no states/
// stages). Tier 2 (per-entity script components ticked by an ECS) reuses the
// same CreateInstance/Invoke primitives once an ECS exists, so it needs no new
// machinery here.

module;
#include "Core/Prelude.h"

export module raptor.runtime.script;

import raptor.core;
import raptor.runtime;
import raptor.script;

namespace rc = raptor::core;
namespace rs = raptor::script;

export namespace raptor::runtime
{
    class ScriptSubsystem final : public Subsystem
    {
    public:
        explicit ScriptSubsystem(rc::RefPtr<rs::IScriptManager> manager,
                                 const rc::TypeRegistry& registry = rc::GlobalTypeRegistry()) noexcept
            : m_manager(rc::Move(manager)), m_registry(&registry) {}

        [[nodiscard]] rs::IScriptManager* Manager() noexcept { return m_manager.Get(); }
        [[nodiscard]] rs::IScriptContext* Context() noexcept { return m_context.Get(); }

        // Compile and run script source in the main context. Defaults to the
        // "main" module so the reflected foreign classes are in scope.
        rc::Status Load(rc::WideStringView source, rc::WideStringView chunkName = u"main")
        {
            return (m_context.Get() != nullptr) ? m_context->Load(source, chunkName)
                                          : rc::Status{ rc::ErrorCode::Internal };
        }

        // Instantiate a script-defined class (see IScriptContext::CreateInstance).
        [[nodiscard]] rc::RefPtr<rs::ScriptObject> CreateInstance(
            rc::WideStringView className, rc::Span<rc::Variant> args)
        {
            return (m_context.Get() != nullptr) ? m_context->CreateInstance(className, args)
                                          : rc::RefPtr<rs::ScriptObject>{};
        }

        // Set (or clear, with null) the global driver object. While set, it
        // receives update(dt) each frame. The subsystem holds a strong reference.
        void SetDriver(rc::RefPtr<rs::ScriptObject> driver) noexcept { m_driver = rc::Move(driver); }
        [[nodiscard]] rs::ScriptObject* Driver() noexcept { return m_driver.Get(); }

        // Forward the per-frame update to the driver script (if any).
        void Update(rc::f32 deltaTime) override
        {
            if (m_driver.Get() != nullptr)
            {
                rc::Variant args[] = { rc::Variant::From<rc::f32>(deltaTime) };
                (void)m_driver->Invoke(u"update", rc::Span<rc::Variant>{ args, 1 });
            }
        }

    protected:
        // Expose the reflected types, then create the main context (which snapshots
        // the types registered up to that point).
        void OnInit() override
        {
            if (m_manager.Get() == nullptr) { return; }
            rs::RegisterReflectedTypes(*m_manager, *m_registry);
            m_context = m_manager->CreateContext();
        }

        void OnShutdown() override
        {
            m_driver.Reset();
            m_context.Reset();
        }

    private:
        rc::RefPtr<rs::IScriptManager> m_manager;
        const rc::TypeRegistry* m_registry;
        rc::RefPtr<rs::IScriptContext> m_context;
        rc::RefPtr<rs::ScriptObject> m_driver;
    };
}
