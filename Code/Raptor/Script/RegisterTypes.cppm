// Raptor Script — :script_register partition
//
// Bridges reflection -> scripting: registers every type in a TypeRegistry with a
// script manager. Call after the engine's RegisterCoreTypes() (and any
// higher-layer registration) to expose them to scripts in one shot.

module;
#include "Core/Prelude.h"

export module raptor.script:script_register;

import raptor.core;
import :script_manager;

namespace rc = raptor::core;

export namespace raptor::script
{
    inline void RegisterReflectedTypes(IScriptManager& manager,
                                       const rc::TypeRegistry& registry = rc::GlobalTypeRegistry())
    {
        for (const rc::TypeInfo* type : registry.All())
        {
            manager.RegisterType(*type);
        }
    }
}
