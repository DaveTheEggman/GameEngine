// Draconic::Particles - reflection implementation unit (batch 1): the range leaf value types +
// the flat/range particle module classes, so the module types stop being tooling-invisible
// (reflection track P2). The particle editor page stays bespoke - this reflection is for
// scriptability/tooling visibility, not a generated inspector.
//
// DRACONIC_REFLECT bodies live out of the ParticleModules.cppm interface (GCC module hygiene:
// property member-pointers make GCC emit a gcm cluster). A module's authored config fields are
// reflected here; the runtime-set "hidden" fields (emitterPosition/emitterVelocity) are not.
// Range struct fields are Nested (particle value types are not script-marshalled leaves - they
// are traversed via reflection); Core math types (Float3) are plain Properties. Modules that use
// EmissionShape (Position/Velocity) or curves (the OverLifetime behaviors) are a later batch.

module;
#include "Draconic.Core/Prelude.h"
#include "Draconic.Core/Reflection/Reflect.h"

module draconic.particles;

import draconic.core;

using namespace draconic::core;

namespace draconic::particles
{
    // ---- range leaf value types --------------------------------------------------------------
    DRACONIC_REFLECT_VALUE(RangeFloat, "draconic::particles")
    {
        builder.Property<&RangeFloat::min>("min").Property<&RangeFloat::max>("max");
    }
    DRACONIC_REFLECT_VALUE(RangeFloat2, "draconic::particles")
    {
        builder.Property<&RangeFloat2::min>("min").Property<&RangeFloat2::max>("max");
    }
    DRACONIC_REFLECT_VALUE(RangeColor, "draconic::particles")
    {
        builder.Property<&RangeColor::min>("min").Property<&RangeColor::max>("max");
    }

    // ---- emission shape (flat struct + its type discriminator) -------------------------------
    DRACONIC_REFLECT_ENUM(EmissionShapeType, "draconic::particles")
    {
        builder.Value("Point", EmissionShapeType::Point);
        builder.Value("Sphere", EmissionShapeType::Sphere);
        builder.Value("Hemisphere", EmissionShapeType::Hemisphere);
        builder.Value("Box", EmissionShapeType::Box);
        builder.Value("Cone", EmissionShapeType::Cone);
        builder.Value("Ring", EmissionShapeType::Ring);
        builder.Value("Circle", EmissionShapeType::Circle);
        builder.Value("Edge", EmissionShapeType::Edge);
    }
    DRACONIC_REFLECT_VALUE(EmissionShape, "draconic::particles")
    {
        builder.Property<&EmissionShape::type>("type")
            .PropAttribute("displayName", String(u8"Shape"))
            .Property<&EmissionShape::radius>("radius")
            .Property<&EmissionShape::extents>("extents")
            .Property<&EmissionShape::angle>("angle")
            .Property<&EmissionShape::arc>("arc")
            .Property<&EmissionShape::emitFromShell>("emitFromShell");
    }

    // ---- initializers using the emission shape -----------------------------------------------
    DRACONIC_REFLECT(PositionInitializer, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Position"))
            .Nested<&PositionInitializer::shape>("shape")
            .Property<&PositionInitializer::localSpace>("localSpace");
    }
    DRACONIC_REFLECT(VelocityInitializer, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Velocity"))
            .Property<&VelocityInitializer::baseVelocity>("baseVelocity")
            .Property<&VelocityInitializer::randomness>("randomness")
            .Property<&VelocityInitializer::shapeDirectionSpeed>("shapeDirectionSpeed")
            .Property<&VelocityInitializer::velocityInheritance>("velocityInheritance")
            .Nested<&VelocityInitializer::shape>("shape");
    }

    // ---- initializers (flat / range) ---------------------------------------------------------
    DRACONIC_REFLECT(LifetimeInitializer, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Lifetime"))
            .Nested<&LifetimeInitializer::lifetime>("lifetime");
    }
    DRACONIC_REFLECT(ColorInitializer, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Color"))
            .Nested<&ColorInitializer::color>("color");
    }
    DRACONIC_REFLECT(SizeInitializer, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Size")).Nested<&SizeInitializer::size>("size");
    }
    DRACONIC_REFLECT(RotationInitializer, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Rotation"))
            .Nested<&RotationInitializer::rotation>("rotation")
            .Nested<&RotationInitializer::rotationSpeed>("rotationSpeed");
    }
    DRACONIC_REFLECT(MeshOrientationInitializer, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Mesh Orientation"))
            .Property<&MeshOrientationInitializer::randomAxis>("randomAxis")
            .Property<&MeshOrientationInitializer::fixedAxis>("fixedAxis");
    }

    // ---- behaviors (flat / vector force fields) ----------------------------------------------
    DRACONIC_REFLECT(GravityBehavior, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Gravity"))
            .Property<&GravityBehavior::multiplier>("multiplier")
            .Property<&GravityBehavior::direction>("direction");
    }
    DRACONIC_REFLECT(DragBehavior, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Drag")).Property<&DragBehavior::drag>("drag");
    }
    DRACONIC_REFLECT(WindBehavior, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Wind"))
            .Property<&WindBehavior::force>("force")
            .Property<&WindBehavior::turbulence>("turbulence");
    }
    DRACONIC_REFLECT(TurbulenceBehavior, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Turbulence"))
            .Property<&TurbulenceBehavior::strength>("strength")
            .Property<&TurbulenceBehavior::frequency>("frequency")
            .Property<&TurbulenceBehavior::speed>("speed");
    }
    DRACONIC_REFLECT(VortexBehavior, "draconic::particles")
    {
        builder.Attribute("displayName", String(u8"Vortex"))
            .Property<&VortexBehavior::strength>("strength")
            .Property<&VortexBehavior::center>("center")
            .Property<&VortexBehavior::axis>("axis");
    }

    // Registers the range leaf value types (the modules' StaticType() self-builds via
    // DRACONIC_REFLECT). Idempotent; call from RegisterParticleModules().
    void RegisterParticleModuleReflection()
    {
        static const bool once = []()
        {
            DraconicRegisterValue_RangeFloat();
            DraconicRegisterValue_RangeFloat2();
            DraconicRegisterValue_RangeColor();
            DraconicRegisterEnum_EmissionShapeType();
            DraconicRegisterValue_EmissionShape();
            return true;
        }();
        (void)once;
    }
}
