// Pipeline::Input - reflection implementation unit: InputMapAsset's reflected surface.
//
// InputMapAsset wraps an InputMap by value; InputMap is a nested list-of-lists (sets -> actions ->
// bindings). It is exposed as a NESTED property (TypeBuilder::Nested) so tooling/scripting can
// traverse the whole tree in place via container reflection, without marshalling it by value. The
// input EDITOR page stays bespoke - this reflection is for scriptability, not a generated inspector.
// DRACONIC_REFLECT out of the interface (GCC module hygiene). Reflection track P2.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module input.pipeline;

import foundation.core;
import pipeline.core;
import foundation.input; // InputMap (the nested reflected type)

using namespace foundation::core;
using namespace foundation::input;

namespace pipeline{
    DRACONIC_REFLECT(InputMapAsset, "rtti::pipeline::input")
    {
        builder.Attribute("displayName", String(u8"Input Map"))
            .Attribute("category", String(u8"Input"))
            .Nested<&InputMapAsset::m_map>("map")
            .PropAttribute("displayName", String(u8"Map"));
    }
}
