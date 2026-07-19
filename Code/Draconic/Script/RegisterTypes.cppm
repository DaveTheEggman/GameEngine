// Draconic Script - :script_register partition
//
// Bridges reflection -> scripting: registers every type in a TypeRegistry with a
// script manager. Call after the engine's RegisterCoreTypes() (and any
// higher-layer registration) to expose them to scripts in one shot.

module;
#include "Core/Prelude.h"

export module draconic.script:script_register;

import draconic.core;
import :script_manager;

namespace core = draconic::core;

export namespace draconic::script
{
    inline void RegisterReflectedTypes(IScriptManager& manager,
                                       const core::TypeRegistry& registry = core::GlobalTypeRegistry())
    {
        for (const core::TypeInfo* type : registry.All())
        {
            manager.RegisterType(*type);
        }
        manager.FinalizeTypes();   // two-phase backends emit here (declare-all, then bind)
    }
}
