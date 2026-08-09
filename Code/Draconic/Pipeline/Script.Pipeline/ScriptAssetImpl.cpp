// Pipeline::Script - reflection implementation unit: ScriptClassAsset's reflected surface.
//
// Kept OUT of the ScriptAsset.cppm interface (REFLECT_MEMBERS bodies make GCC emit a gcm cluster;
// see gcc-module-interface-hygiene). ScriptClassAsset::StaticType() gains its `language` property
// here. No enums, so the reflection rides StaticType() with no registrar change. Reflection
// track P1.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module script.pipeline;

import foundation.core;
import pipeline.core;

using namespace foundation::core;
using namespace foundation::script;

namespace pipeline{
    REFLECT_MEMBERS(ScriptClassAsset, "rtti::pipeline::script")
    {
        builder.Attribute("displayName", String(u8"Script"))
            .Attribute("category", String(u8"Scripting"))
            .Property<&ScriptClassAsset::language>("language")
            .PropAttribute("displayName", String(u8"Language"))
            .PropAttribute("description", String(u8"Backend id (e.g. \"wren\", \"angelscript\")"));
    }
}
