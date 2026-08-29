// Foundation::Materials - reflection implementation unit: the material render-state enums.
//
// Reflected in their OWNING module (foundation.materials) so any consumer of a reflected
// MaterialSource property whose type is one of these (blendMode/depthMode/cullMode/vertexLayout,
// retyped from u8) sees a proper enum - IsEnum + named values, so tooling can render a name
// dropdown instead of a raw integer. REFLECT_ENUM bodies live out of the interface
// (GCC module hygiene). RegisterMaterialsTypeReflection() is idempotent; wire it from a startup
// registrar (RegisterMaterialAsset does).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module foundation.materials;

import foundation.core;

using namespace foundation::core;

namespace foundation::materials
{
    REFLECT_ENUM(BlendMode, "rtti::materials")
    {
        builder.Value("Opaque", BlendMode::Opaque);
        builder.Value("Masked", BlendMode::Masked);
        builder.Value("AlphaBlend", BlendMode::AlphaBlend);
        builder.Value("Additive", BlendMode::Additive);
        builder.Value("Multiply", BlendMode::Multiply);
        builder.Value("PremultipliedAlpha", BlendMode::PremultipliedAlpha);
    }

    REFLECT_ENUM(DepthMode, "rtti::materials")
    {
        builder.Value("Disabled", DepthMode::Disabled);
        builder.Value("ReadWrite", DepthMode::ReadWrite);
        builder.Value("ReadOnly", DepthMode::ReadOnly);
        builder.Value("WriteOnly", DepthMode::WriteOnly);
    }

    REFLECT_ENUM(CullModeConfig, "rtti::materials")
    {
        builder.Value("None", CullModeConfig::None);
        builder.Value("Back", CullModeConfig::Back);
        builder.Value("Front", CullModeConfig::Front);
    }

    REFLECT_ENUM(VertexLayoutType, "rtti::materials")
    {
        builder.Value("None", VertexLayoutType::None);
        builder.Value("PositionOnly", VertexLayoutType::PositionOnly);
        builder.Value("PositionUVColor", VertexLayoutType::PositionUVColor);
        builder.Value("MeshNoTangent", VertexLayoutType::MeshNoTangent);
        builder.Value("Mesh", VertexLayoutType::Mesh);
        builder.Value("SkinnedMesh", VertexLayoutType::SkinnedMesh);
        builder.Value("Custom", VertexLayoutType::Custom);
    }

    void RegisterMaterialsTypeReflection()
    {
        static const bool once = []()
        {
            RttiRegisterEnum_BlendMode();
            RttiRegisterEnum_DepthMode();
            RttiRegisterEnum_CullModeConfig();
            RttiRegisterEnum_VertexLayoutType();
            return true;
        }();
        (void)once;
    }
}
