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
    class IScriptContext : public rc::Object
    {
    public:
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
    };
}
