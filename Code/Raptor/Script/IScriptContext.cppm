// Raptor Script — :script_context partition
//
// IScriptContext: an isolated script execution environment. Everything crossing
// the boundary uses Core's reflection currency — Variant for values/objects,
// TypeInfo for types — so the interface is VM-agnostic. Backends (Lua, ...) are
// plugins implementing this; see Documentation/Planning.

module;
#include "Core/Prelude.h"

export module raptor.script:script_context;

import raptor.core;

namespace rc = raptor::core;

export namespace raptor::script
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
        rc::StringView module;  // may be empty
        rc::i32 line;           // 1-based source line, or -1 if unknown
        rc::StringView message;
    };

    // Host-provided sink for script errors (compile + runtime). Non-owning: the
    // host manages its lifetime and outlives the context it's attached to.
    class IScriptErrorHandler
    {
    public:
        virtual ~IScriptErrorHandler() = default;
        virtual void OnError(const ScriptError& error) = 0;
    };

    // A handle to a live script-side object — typically an instance of a
    // script-defined class. Reference-counted; it keeps its owning context alive
    // for as long as it exists. This is the shared primitive script integration
    // builds on: a global "driver" object the runtime ticks (tier 1), or — later,
    // when an ECS exists — per-entity script components an ECS system ticks.
    class ScriptObject : public rc::Object
    {
    public:
        // Invoke a method on this object with reflected args. Returns its result
        // (empty Variant for a void method), or an error if the method is missing
        // or the script faults.
        [[nodiscard]] virtual rc::Result<rc::Variant> Invoke(
            rc::StringView method, rc::Span<rc::Variant> args) = 0;
    };

    class IScriptContext : public rc::Object
    {
    public:
        // Sets (or clears, with nullptr) the error sink. When unset, backends
        // report errors to the console.
        virtual void SetErrorHandler(IScriptErrorHandler* handler) = 0;

        // Compile and run a chunk of script source. NotSupported if the backend
        // has no compiler (e.g. it only loads precompiled blobs).
        virtual rc::Status Load(rc::StringView source, rc::StringView chunkName) = 0;

        // Globals are exchanged as Variants (values or objects).
        virtual void SetGlobal(rc::StringView name, const rc::Variant& value) = 0;
        [[nodiscard]] virtual rc::Variant GetGlobal(rc::StringView name) = 0;

        // True if a callable global by that name exists.
        [[nodiscard]] virtual bool HasFunction(rc::StringView name) const = 0;

        // Call a global function with reflected args; returns its result (void
        // -> empty Variant), or an error if missing / on a script fault.
        [[nodiscard]] virtual rc::Result<rc::Variant> Call(
            rc::StringView function, rc::Span<rc::Variant> args) = 0;

        // Instantiate a script-defined class by name, passing reflected
        // constructor args. Returns null if the class is unknown or construction
        // faults. The returned object outlives this call and retains the context.
        [[nodiscard]] virtual rc::RefPtr<ScriptObject> CreateInstance(
            rc::StringView className, rc::Span<rc::Variant> args) = 0;
    };
}
