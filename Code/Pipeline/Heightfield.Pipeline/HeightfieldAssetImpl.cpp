// Pipeline::Heightfield - reflection implementation unit: HeightfieldAsset's reflected surface.
//
// Kept OUT of the HeightfieldAsset.cppm interface (REFLECT_MEMBERS bodies make GCC emit a gcm
// cluster; see gcc-module-interface-hygiene). HeightfieldAsset::StaticType() gains its size /
// worldSize / minY / maxY properties here.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module heightfield.pipeline;

import foundation.core;
import pipeline.core;

using namespace foundation::core;

namespace pipeline
{
    REFLECT_MEMBERS(HeightfieldAsset, "rtti::pipeline::heightfield")
    {
        builder.Attribute("displayName", String(u8"Heightfield"))
            .Attribute("category", String(u8"Terrain"))
            .Property<&HeightfieldAsset::size>("size")
            .PropAttribute("displayName", String(u8"Grid Size (64k+1)"))
            .Property<&HeightfieldAsset::worldSize>("worldSize")
            .PropAttribute("displayName", String(u8"World Size (XZ)"))
            .Property<&HeightfieldAsset::minY>("minY")
            .PropAttribute("displayName", String(u8"Min Height"))
            .Property<&HeightfieldAsset::maxY>("maxY")
            .PropAttribute("displayName", String(u8"Max Height"));
    }
}
