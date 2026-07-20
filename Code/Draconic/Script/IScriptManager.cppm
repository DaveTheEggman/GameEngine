// Draconic Script - :script_manager partition
//
// IScriptManager: the VM. Reflected types are registered with it (it walks each
// TypeInfo to build the backend's bindings), and it creates execution contexts.
// One backend (Lua, ...) implements this per VM; backends are plugins.

module;
#include "Core/Prelude.h"

export module draconic.script:script_manager;

import draconic.core;
import :script_context;

namespace core = draconic::core;

export namespace draconic::script
{
    /// Optional backend features (scripting.md B4), declared per backend and consumed
    /// contract-first: a consumer CHECKS the flag and degrades cleanly - a backend
    /// without Coroutines still runs behaviors, it just has no coroutine scheduler.
    enum class ScriptCapabilities : core::u32
    {
        None       = 0,
        Coroutines = 1 << 0, // cooperative coroutines (wait/waitUntil scheduler) - implemented
                             // host-side per backend (each backend uses its own primitive)
        Debugger   = 1 << 1, // step-debug seam (none implemented yet)
        Profiler   = 1 << 2, // VM-level profiling hooks (none implemented yet)
    };
    inline constexpr ScriptCapabilities operator|(ScriptCapabilities a, ScriptCapabilities b)
    {
        return static_cast<ScriptCapabilities>(static_cast<core::u32>(a) | static_cast<core::u32>(b));
    }
    inline constexpr bool HasScriptCapability(ScriptCapabilities value, ScriptCapabilities flag)
    {
        return (static_cast<core::u32>(value) & static_cast<core::u32>(flag)) != 0;
    }

    class IScriptManager : public core::Object
    {
    public:
        // Expose a reflected type to scripts. The backend introspects the
        // TypeInfo (properties / methods / constructors / constants) and installs
        // the corresponding bindings.
        virtual void RegisterType(const core::TypeInfo& type) = 0;

        /// Called once after ALL RegisterType calls, before the first CreateContext
        /// (RegisterReflectedTypes drives it). Backends that need two-phase emission -
        /// AngelScript must DECLARE every object type before any member of any type is
        /// registered, or everything has to arrive in strict dependency order - defer
        /// their emission to here: declare-all-types, then bind-all-members. Backends
        /// with lazy emitters (Wren materializes at context creation) no-op.
        virtual void FinalizeTypes() {}

        // Create a fresh, isolated execution context. Contexts see the classes
        // registered up to their creation.
        [[nodiscard]] virtual core::RefPtr<IScriptContext> CreateContext() = 0;

        // Single-step garbage collection, for backends that need it kept small in
        // real-time loops. Default: no-op.
        virtual void CollectGarbage() {}

        /// Resume every coroutine whose wait has elapsed, accumulating `deltaSeconds`;
        /// drop the ones that complete or fault. The scheduler lives host-side inside
        /// the backend (each backend on its own primitive); the subsystem calls this
        /// ONCE per simulated frame. A backend without ScriptCapabilities::Coroutines
        /// leaves this a no-op - the battery certifies the behavior, the flag only
        /// advertises it.
        virtual void AdvanceCoroutines(core::f64 deltaSeconds) { (void)deltaSeconds; }

        /// Stop and drop every coroutine owned by `instance` (its behavior is being
        /// disabled or destroyed). No-op on a backend without the capability.
        virtual void CancelCoroutinesFor(ScriptObject& instance) { (void)instance; }

        /// The backend's OPTIONAL feature set. Required behavior is certified by the
        /// conformance battery instead - never flagged here.
        [[nodiscard]] virtual ScriptCapabilities Capabilities() const
        {
            return ScriptCapabilities::None;
        }

        /// Assemble the ONE behavior module's source from the loaded class SOURCES. The
        /// run host owns the neutral generation/state bookkeeping and compiles the result;
        /// the LANGUAGE-SPECIFIC framing lives HERE, per backend, so no language syntax
        /// leaks into the neutral libraries (scripting.md §7.5). A backend whose reflected
        /// types live in a separate module prepends its own facade-import prelude + any
        /// coroutine base; a backend with globally-visible types needs none. The default
        /// is a plain newline-joined concatenation - the safe behavior for a backend that
        /// needs no framing at all.
        [[nodiscard]] virtual core::String AssembleBehaviorModuleSource(
            core::Span<const core::StringView> classSources) const
        {
            core::String moduleSource;
            for (const core::StringView& source : classSources)
            {
                moduleSource += source;
                moduleSource += u8"\n";
            }
            return moduleSource;
        }
    };
}
