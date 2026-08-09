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
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module foundation.particles;

import foundation.core;

using namespace foundation::core;

namespace foundation::particles
{
    // ---- range leaf value types --------------------------------------------------------------
    DRACONIC_REFLECT_VALUE(RangeFloat, "rtti::particles")
    {
        builder.Property<&RangeFloat::min>("min").Property<&RangeFloat::max>("max");
    }
    DRACONIC_REFLECT_VALUE(RangeFloat2, "rtti::particles")
    {
        builder.Property<&RangeFloat2::min>("min").Property<&RangeFloat2::max>("max");
    }
    DRACONIC_REFLECT_VALUE(RangeColor, "rtti::particles")
    {
        builder.Property<&RangeColor::min>("min").Property<&RangeColor::max>("max");
    }

    // ---- emission shape (flat struct + its type discriminator) -------------------------------
    DRACONIC_REFLECT_ENUM(EmissionShapeType, "rtti::particles")
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
    DRACONIC_REFLECT_VALUE(EmissionShape, "rtti::particles")
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
    DRACONIC_REFLECT(PositionInitializer, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Position"))
            .Nested<&PositionInitializer::shape>("shape")
            .Property<&PositionInitializer::localSpace>("localSpace");
    }
    DRACONIC_REFLECT(VelocityInitializer, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Velocity"))
            .Property<&VelocityInitializer::baseVelocity>("baseVelocity")
            .Property<&VelocityInitializer::randomness>("randomness")
            .Property<&VelocityInitializer::shapeDirectionSpeed>("shapeDirectionSpeed")
            .Property<&VelocityInitializer::velocityInheritance>("velocityInheritance")
            .Nested<&VelocityInitializer::shape>("shape");
    }

    // ---- initializers (flat / range) ---------------------------------------------------------
    DRACONIC_REFLECT(LifetimeInitializer, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Lifetime"))
            .Nested<&LifetimeInitializer::lifetime>("lifetime");
    }
    DRACONIC_REFLECT(ColorInitializer, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Color"))
            .Nested<&ColorInitializer::color>("color");
    }
    DRACONIC_REFLECT(SizeInitializer, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Size")).Nested<&SizeInitializer::size>("size");
    }
    DRACONIC_REFLECT(RotationInitializer, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Rotation"))
            .Nested<&RotationInitializer::rotation>("rotation")
            .Nested<&RotationInitializer::rotationSpeed>("rotationSpeed");
    }
    DRACONIC_REFLECT(MeshOrientationInitializer, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Mesh Orientation"))
            .Property<&MeshOrientationInitializer::randomAxis>("randomAxis")
            .Property<&MeshOrientationInitializer::fixedAxis>("fixedAxis");
    }

    // ---- behaviors (flat / vector force fields) ----------------------------------------------
    DRACONIC_REFLECT(GravityBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Gravity"))
            .Property<&GravityBehavior::multiplier>("multiplier")
            .Property<&GravityBehavior::direction>("direction");
    }
    DRACONIC_REFLECT(DragBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Drag")).Property<&DragBehavior::drag>("drag");
    }
    DRACONIC_REFLECT(WindBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Wind"))
            .Property<&WindBehavior::force>("force")
            .Property<&WindBehavior::turbulence>("turbulence");
    }
    DRACONIC_REFLECT(TurbulenceBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Turbulence"))
            .Property<&TurbulenceBehavior::strength>("strength")
            .Property<&TurbulenceBehavior::frequency>("frequency")
            .Property<&TurbulenceBehavior::speed>("speed");
    }
    DRACONIC_REFLECT(VortexBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Vortex"))
            .Property<&VortexBehavior::strength>("strength")
            .Property<&VortexBehavior::center>("center")
            .Property<&VortexBehavior::axis>("axis");
    }

    // ---- attractor / radial force / collision (flat) -----------------------------------------
    DRACONIC_REFLECT(AttractorBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Attractor"))
            .Property<&AttractorBehavior::strength>("strength")
            .Property<&AttractorBehavior::position>("position")
            .Property<&AttractorBehavior::radius>("radius");
    }
    DRACONIC_REFLECT(RadialForceBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Radial Force"))
            .Property<&RadialForceBehavior::strength>("strength");
    }

    DRACONIC_REFLECT_VALUE(CollisionPlane, "rtti::particles")
    {
        builder.Property<&CollisionPlane::normal>("normal")
            .Property<&CollisionPlane::distance>("distance");
    }
    DRACONIC_REFLECT_VALUE(CollisionSphere, "rtti::particles")
    {
        builder.Property<&CollisionSphere::center>("center")
            .Property<&CollisionSphere::radius>("radius");
    }
    DRACONIC_REFLECT_VALUE(CollisionBox, "rtti::particles")
    {
        builder.Property<&CollisionBox::center>("center")
            .Property<&CollisionBox::halfExtents>("halfExtents");
    }
    DRACONIC_REFLECT(CollisionBehavior, "rtti::particles")
    {
        // The fixed-capacity shape lists are count-bound inline vectors, reflected as BoundedArray
        // containers (size = the live count clamped to capacity); the counts stay as scalars too.
        builder.Attribute("displayName", String(u8"Collision"))
            .BoundedArray<&CollisionBehavior::planes, &CollisionBehavior::planeCount>("planes")
            .BoundedArray<&CollisionBehavior::spheres, &CollisionBehavior::sphereCount>("spheres")
            .BoundedArray<&CollisionBehavior::boxes, &CollisionBehavior::boxCount>("boxes")
            .Property<&CollisionBehavior::planeCount>("planeCount")
            .Property<&CollisionBehavior::sphereCount>("sphereCount")
            .Property<&CollisionBehavior::boxCount>("boxCount")
            .Property<&CollisionBehavior::radius>("radius")
            .Property<&CollisionBehavior::bounce>("bounce")
            .Property<&CollisionBehavior::friction>("friction")
            .Property<&CollisionBehavior::lifetimeLoss>("lifetimeLoss");
    }

    // ---- curves (count-bound key arrays) + the OverLifetime behaviors ------------------------
    DRACONIC_REFLECT_VALUE(CurveKeyFloat, "rtti::particles")
    {
        builder.Property<&CurveKeyFloat::time>("time")
            .Property<&CurveKeyFloat::value>("value")
            .Property<&CurveKeyFloat::tangentIn>("tangentIn")
            .Property<&CurveKeyFloat::tangentOut>("tangentOut");
    }
    DRACONIC_REFLECT_VALUE(CurveKeyColor, "rtti::particles")
    {
        builder.Property<&CurveKeyColor::time>("time").Property<&CurveKeyColor::color>("color");
    }
    DRACONIC_REFLECT_VALUE(ParticleCurveFloat, "rtti::particles")
    {
        builder.BoundedArray<&ParticleCurveFloat::keys, &ParticleCurveFloat::keyCount>("keys")
            .Property<&ParticleCurveFloat::keyCount>("keyCount");
    }
    DRACONIC_REFLECT_VALUE(ParticleCurveColor, "rtti::particles")
    {
        builder.BoundedArray<&ParticleCurveColor::keys, &ParticleCurveColor::keyCount>("keys")
            .Property<&ParticleCurveColor::keyCount>("keyCount");
    }
    DRACONIC_REFLECT_VALUE(ParticleCurveFloat2, "rtti::particles")
    {
        // Parallel key arrays (times/values/tangents), all count-bound to keyCount.
        builder.BoundedArray<&ParticleCurveFloat2::times, &ParticleCurveFloat2::keyCount>("times")
            .BoundedArray<&ParticleCurveFloat2::values, &ParticleCurveFloat2::keyCount>("values")
            .BoundedArray<&ParticleCurveFloat2::tangentsIn, &ParticleCurveFloat2::keyCount>(
                "tangentsIn")
            .BoundedArray<&ParticleCurveFloat2::tangentsOut, &ParticleCurveFloat2::keyCount>(
                "tangentsOut")
            .Property<&ParticleCurveFloat2::keyCount>("keyCount");
    }

    DRACONIC_REFLECT(ColorOverLifetimeBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Color over Lifetime"))
            .Nested<&ColorOverLifetimeBehavior::curve>("curve");
    }
    DRACONIC_REFLECT(AlphaOverLifetimeBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Alpha over Lifetime"))
            .Nested<&AlphaOverLifetimeBehavior::curve>("curve");
    }
    DRACONIC_REFLECT(SizeOverLifetimeBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Size over Lifetime"))
            .Nested<&SizeOverLifetimeBehavior::curve>("curve");
    }
    DRACONIC_REFLECT(RotationOverLifetimeBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Rotation over Lifetime"))
            .Nested<&RotationOverLifetimeBehavior::curve>("curve");
    }
    DRACONIC_REFLECT(SpeedOverLifetimeBehavior, "rtti::particles")
    {
        builder.Attribute("displayName", String(u8"Speed over Lifetime"))
            .Nested<&SpeedOverLifetimeBehavior::curve>("curve");
    }

    // Registers the range + shape + collision + curve leaf value types (the modules' StaticType()
    // self-builds via DRACONIC_REFLECT). Idempotent; call from RegisterParticleModules().
    void RegisterParticleModuleReflection()
    {
        static const bool once = []()
        {
            DraconicRegisterValue_RangeFloat();
            DraconicRegisterValue_RangeFloat2();
            DraconicRegisterValue_RangeColor();
            DraconicRegisterEnum_EmissionShapeType();
            DraconicRegisterValue_EmissionShape();
            DraconicRegisterValue_CollisionPlane();
            DraconicRegisterValue_CollisionSphere();
            DraconicRegisterValue_CollisionBox();
            DraconicRegisterValue_CurveKeyFloat();
            DraconicRegisterValue_CurveKeyColor();
            DraconicRegisterValue_ParticleCurveFloat();
            DraconicRegisterValue_ParticleCurveColor();
            DraconicRegisterValue_ParticleCurveFloat2();
            // The module arrays are polymorphic: register Array<RefPtr<Base>> as a polymorphic
            // container so tooling recurses into each module's concrete reflected type. Pass the
            // standard serialization create-by-type adapter (the factory flows in as function
            // pointers - reflection never imports serialization).
            RegisterPolymorphicArrayType<ParticleInitializer>(
                &CreateSerializableElement<ParticleInitializer>,
                &CanCreateSerializableElement<ParticleInitializer>);
            RegisterPolymorphicArrayType<ParticleBehavior>(
                &CreateSerializableElement<ParticleBehavior>,
                &CanCreateSerializableElement<ParticleBehavior>);
            return true;
        }();
        (void)once;
    }
}
