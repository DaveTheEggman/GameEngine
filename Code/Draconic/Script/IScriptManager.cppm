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
    class IScriptManager : public core::Object
    {
    public:
        // Expose a reflected type to scripts. The backend introspects the
        // TypeInfo (properties / methods / constructors / constants) and installs
        // the corresponding bindings.
        virtual void RegisterType(const core::TypeInfo& type) = 0;

        // Create a fresh, isolated execution context. Contexts see the classes
        // registered up to their creation.
        [[nodiscard]] virtual core::RefPtr<IScriptContext> CreateContext() = 0;

        // Single-step garbage collection, for backends that need it kept small in
        // real-time loops. Default: no-op.
        virtual void CollectGarbage() {}
    };
}
