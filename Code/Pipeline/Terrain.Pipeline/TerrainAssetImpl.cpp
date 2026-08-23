// Pipeline::Terrain - reflection implementation unit: TerrainAsset's reflected surface.
//
// Kept OUT of the TerrainAsset.cppm interface (REFLECT_MEMBERS bodies make GCC emit a gcm cluster;
// see gcc-module-interface-hygiene). Proper heightfield/texture PICKERS ride the phase-2
// Editor.Terrain page; P1 reflects the scalar fields (the cast-shadows flag is the one live toggle).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module terrain.pipeline;

import foundation.core;
import pipeline.core;

using namespace foundation::core;

namespace pipeline
{
    REFLECT_MEMBERS(TerrainAsset, "rtti::pipeline::terrain")
    {
        builder.Attribute("displayName", String(u8"Terrain"))
            .Attribute("category", String(u8"Terrain"))
            .Property<&TerrainAsset::castShadows>("castShadows")
            .PropAttribute("displayName", String(u8"Cast Shadows"));
    }
}
