// Pipeline::Core - reflection implementation unit: the Asset base's reflected surface.
//
// Kept OUT of the Asset.cppm interface (DRACONIC_REFLECT bodies make GCC emit a gcm cluster; see
// gcc-module-interface-hygiene). Asset::StaticType() gains its fileName property here, so every
// concrete asset inherits it through the base chain (FindProperty walks bases). RegisterAssetReflection
// also registers SourcePath's reflection (fileName's type). Reflection track P1.

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module pipeline.core;

import foundation.core;
import foundation.vfs;

using namespace foundation::core;

namespace pipeline
{
    DRACONIC_REFLECT(Asset, "rtti::pipeline::asset")
    {
        builder.Property<&Asset::fileName>("fileName")
            .PropAttribute("displayName", String(u8"Source File"))
            .PropAttribute("description",
                           String(u8"Source file, relative to the sources mount (empty = embedded)"));
    }

    void RegisterAssetReflection()
    {
        static const bool once = []()
        {
            foundation::vfs::RegisterVFSReflection(); // SourcePath (fileName's type)
            GlobalTypeRegistry().Register(Asset::StaticType(), TypeDomain(u8"Editor"));
            return true;
        }();
        (void)once;
    }
}
