// Pipeline::Materials - reflection implementation unit: MaterialAsset's reflected surface.
//
// Kept OUT of the MaterialAsset.cppm interface (DRACONIC_REFLECT bodies make GCC emit a gcm
// cluster; see gcc-module-interface-hygiene). MaterialAsset wraps a MaterialSource BY VALUE, and
// MaterialSource derives Object (RefCounted deletes its copy ctor) so it cannot marshal through a
// Variant - it is exposed as a NESTED property (TypeBuilder::Nested): tooling reaches the member
// in place via address and recurses into MaterialSource's own reflected properties. This is the
// reflection-track "living proof" of the nested-member mechanism. Reflection track P1.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module materials.pipeline;

import foundation.core;
import pipeline.core;
import foundation.materials.resource; // MaterialSource (the nested reflected type)

using namespace foundation::core;
using namespace foundation::materials;

namespace pipeline{
    DRACONIC_REFLECT(MaterialAsset, "rtti::pipeline::materials")
    {
        builder.Attribute("displayName", String(u8"Material"))
            .Attribute("category", String(u8"Materials"))
            .Nested<&MaterialAsset::source>("source")
            .PropAttribute("displayName", String(u8"Source"));
    }
}
