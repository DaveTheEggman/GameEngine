// Draconic Script - :script_context partition
//
// IScriptContext: an isolated script execution environment. Everything crossing
// the boundary uses Core's reflection currency - Variant for values/objects,
// TypeInfo for types - so the interface is VM-agnostic. Backends (Lua, ...) are
// plugins implementing this; see Documentation/Planning.

module;
#include "Core/Prelude.h"

export module draconic.script:script_context;

import draconic.core;

namespace core = draconic::core;

export namespace draconic::script
{
    enum class ScriptErrorKind
    {
        Compile, // failed to compile source
        Runtime, // threw / aborted while running
    };

    // A script error. The string views are valid only for the duration of the
    // OnError callback (copy out if you need to keep them).
    struct ScriptError
    {
        ScriptErrorKind kind;
        core::StringView module;  // may be empty
        core::i32 line;           // 1-based source line, or -1 if unknown
        core::StringView message;
    };

    // Host-provided sink for script errors (compile + runtime). Non-owning: the
    // host manages its lifetime and outlives the context it's attached to.
    class IScriptErrorHandler
    {
    public:
        virtual ~IScriptErrorHandler() = default;
        virtual void OnError(const ScriptError& error) = 0;
    };

    // A handle to a live script-side object - typically an instance of a
    // script-defined class. Reference-counted; it keeps its owning context alive
    // for as long as it exists. This is the shared primitive script integration
    // builds on: a global "driver" object the runtime ticks (tier 1), or - later,
    // when an ECS exists - per-entity script components an ECS system ticks.
    class ScriptObject : public core::Object
    {
    public:
        // Invoke a method on this object with reflected args. Returns its result
        // (empty Variant for a void method), or an error if the method is missing
        // or the script faults.
        [[nodiscard]] virtual core::Result<core::Variant> Invoke(
            core::StringView method, core::Span<core::Variant> args) = 0;
    };

    class IScriptContext : public core::Object
    {
    public:
        // Sets (or clears, with nullptr) the error sink. When unset, backends
        // report errors to the console.
        virtual void SetErrorHandler(IScriptErrorHandler* handler) = 0;

        // Compile and run a chunk of script source. NotSupported if the backend
        // has no compiler (e.g. it only loads precompiled blobs).
        virtual core::Status Load(core::StringView source, core::StringView chunkName) = 0;

        // Globals are exchanged as Variants (values or objects).
        virtual void SetGlobal(core::StringView name, const core::Variant& value) = 0;
        [[nodiscard]] virtual core::Variant GetGlobal(core::StringView name) = 0;

        // True if a callable global by that name exists.
        [[nodiscard]] virtual bool HasFunction(core::StringView name) const = 0;

        // Call a global function with reflected args; returns its result (void
        // -> empty Variant), or an error if missing / on a script fault.
        [[nodiscard]] virtual core::Result<core::Variant> Call(
            core::StringView function, core::Span<core::Variant> args) = 0;

        // Instantiate a script-defined class by name, passing reflected
        // constructor args. Returns null if the class is unknown or construction
        // faults. The returned object outlives this call and retains the context.
        [[nodiscard]] virtual core::RefPtr<ScriptObject> CreateInstance(
            core::StringView className, core::Span<core::Variant> args) = 0;

        // ---- host services (per-context) ----
        // Name-keyed host objects native facades resolve DURING a scripted call (via
        // CurrentScriptContext below). This is how engine singletons reach scripts
        // without process globals: each context binds its OWN service set - two
        // contexts can see two different input runtimes (players, editor vs game).
        void SetService(core::StringView name, void* service)
        {
            m_services.InsertOrAssign(core::String(name), service);
        }
        [[nodiscard]] void* GetService(core::StringView name) const
        {
            void* const* found = m_services.Find(core::String(name));
            return found != nullptr ? *found : nullptr;
        }

    private:
        core::HashMap<core::String, void*> m_services;
    };

    // The context whose script code is EXECUTING right now on this thread (null outside
    // scripted calls). Backends push it around every reflected dispatch; native facades
    // (the Input class) resolve their per-context services through it. Nesting-safe
    // (script -> native -> script restores the previous).
    namespace detail
    {
        inline thread_local IScriptContext* g_currentContext = nullptr;
    }
    [[nodiscard]] inline IScriptContext* CurrentScriptContext() noexcept
    {
        return detail::g_currentContext;
    }
    class ScriptCallScope
    {
    public:
        explicit ScriptCallScope(IScriptContext* context) noexcept
            : m_previous(detail::g_currentContext)
        {
            detail::g_currentContext = context;
        }
        ~ScriptCallScope() { detail::g_currentContext = m_previous; }
        ScriptCallScope(const ScriptCallScope&) = delete;
        ScriptCallScope& operator=(const ScriptCallScope&) = delete;

    private:
        IScriptContext* m_previous;
    };
}
