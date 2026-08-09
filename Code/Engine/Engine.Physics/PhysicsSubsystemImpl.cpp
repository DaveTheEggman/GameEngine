// Engine::Physics - implementation unit: the render-frame drive (interpolation
// + debug wireframes) and the component reflection bodies. Both live OUTSIDE the interface
// for GCC: heavy render imports stay out of the interface's module graph, and the
// REFLECT_* macros in the :components partition made GCC emit a gcm with an
// unreadable cluster (every -fno-module-lazy consumer then failed to import the module).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include <cmath>

module engine.physics;

import foundation.core;
import foundation.runtime;
import foundation.scene;
import foundation.resource;
import foundation.physics;
import foundation.physics.resource;
import foundation.render;
import engine.render;
import foundation.materials;
import foundation.script.facades; // RegisterExtraFacadeName (Physics into the behavior prelude)

using namespace foundation::core;
using namespace foundation::physics;
namespace core = foundation::core;

namespace engine::physics
{
    namespace
    {
        // Body wireframes: green = awake dynamic, grey = sleeping, blue = static/kinematic,
        // yellow = trigger.
        void DrawPhysicsDebug(PhysicsSceneSystem& system, foundation::render::debug::DebugDraw& draw)
        {
            PhysicsWorld* world = system.World();
            foundation::scene::Scene* scene = system.ScenePtr();
            if (world == nullptr || scene == nullptr)
            {
                return;
            }
            auto* bodies = scene->GetSystem<RigidBodyComponentManager>();
            if (bodies == nullptr)
            {
                return;
            }
            bodies->ForEach(
                [&](RigidBodyComponent& c, foundation::scene::EntityHandle e)
                {
                    if (!c.body.IsValid())
                    {
                        return;
                    }
                    Float3 position;
                    Quaternion rotation;
                    world->GetBodyTransform(c.body, position, rotation);
                    const Color color =
                        c.isTrigger ? Color{1.0f, 0.8f, 0.2f, 1.0f}
                        : c.motion == MotionKind::Dynamic
                            ? (world->IsActive(c.body) ? Color{0.3f, 1.0f, 0.4f, 1.0f}
                                                       : Color{0.5f, 0.6f, 0.5f, 1.0f})
                            : Color{0.4f, 0.6f, 1.0f, 1.0f};
                    const Float4x4 worldMatrix =
                        Transform{position, rotation, Float3{1, 1, 1}}.ToMatrix();
                    switch (c.shape)
                    {
                    case ShapeKind::Box:
                        draw.DrawTransformedBox(
                            Float3{-c.halfExtents.x, -c.halfExtents.y, -c.halfExtents.z},
                            c.halfExtents, worldMatrix, color);
                        break;
                    case ShapeKind::Sphere:
                        draw.DrawWireSphere(position, c.radius, color);
                        break;
                    case ShapeKind::Capsule:
                        draw.DrawWireSphere(position, c.radius, color);
                        draw.DrawTransformedBox(
                            Float3{-c.radius, -(c.halfHeight + c.radius), -c.radius},
                            Float3{c.radius, c.halfHeight + c.radius, c.radius}, worldMatrix,
                            color);
                        break;
                    case ShapeKind::Plane:
                    {
                        // A bounded grid patch reads better than one huge quad.
                        const f32 extent = c.planeHalfExtent < 25.0f ? c.planeHalfExtent : 25.0f;
                        const i32 kCells = 10;
                        for (i32 g = -kCells; g <= kCells; ++g)
                        {
                            const f32 offset = extent * static_cast<f32>(g) / kCells;
                            draw.DrawLine(TransformPoint(Float3{offset, 0, -extent}, worldMatrix),
                                          TransformPoint(Float3{offset, 0, extent}, worldMatrix),
                                          color);
                            draw.DrawLine(TransformPoint(Float3{-extent, 0, offset}, worldMatrix),
                                          TransformPoint(Float3{extent, 0, offset}, worldMatrix),
                                          color);
                        }
                        break;
                    }
                    case ShapeKind::Cooked:
                        if (const CollisionShape* cooked = c.collisionShape.Get())
                        {
                            // Outline is authored unit-scale; re-apply the entity's scale.
                            Float3 sp, ss;
                            Quaternion sr;
                            const Float4x4 shapeMatrix =
                                Decompose(scene->GetWorldMatrix(e), sp, sr, ss)
                                    ? Transform{position, rotation, ss}.ToMatrix()
                                    : worldMatrix;
                            const Array<Float3>& outline = cooked->outline;
                            for (usize t = 0; t + 2 < outline.Size(); t += 3)
                            {
                                const Float3 a = TransformPoint(outline[t + 0], shapeMatrix);
                                const Float3 b = TransformPoint(outline[t + 1], shapeMatrix);
                                const Float3 d = TransformPoint(outline[t + 2], shapeMatrix);
                                draw.DrawLine(a, b, color);
                                draw.DrawLine(b, d, color);
                                draw.DrawLine(d, a, color);
                            }
                        }
                        break;
                    }
                });

            if (auto* characters = scene->GetSystem<CharacterComponentManager>())
            {
                characters->ForEach(
                    [&](CharacterComponent& c, foundation::scene::EntityHandle)
                    {
                        if (!c.character.IsValid())
                        {
                            return;
                        }
                        const Float3 position = world->CharacterPosition(c.character);
                        const Color color = c.ground == CharacterGround::OnGround
                                                ? Color{0.2f, 0.9f, 0.9f, 1.0f}
                                                : Color{0.9f, 0.5f, 0.9f, 1.0f};
                        draw.DrawWireSphere(
                            Float3{position.x, position.y + c.halfHeight, position.z}, c.radius,
                            color);
                        draw.DrawWireSphere(
                            Float3{position.x, position.y - c.halfHeight, position.z}, c.radius,
                            color);
                        draw.DrawWireBoxCenter(position, Float3{c.radius, c.halfHeight, c.radius},
                                               color);
                    });
            }
        }
    }

    void PhysicsSubsystem::Update(f32)
    {
        foundation::runtime::Context* context = GetContext();
        if (context == nullptr)
        {
            return;
        }
        auto* render = context->GetSubsystem<engine::render::RenderSubsystem>();
        for (const SceneEntry& entry : Systems())
        {
            // Per-scene alpha: each scene steps on its OWN accumulator/time scale.
            entry.system->ApplyInterpolation(entry.scene->FixedAlpha());
            if (render != nullptr && entry.system->Settings().debugDraw)
            {
                DrawPhysicsDebug(*entry.system, render->DebugScene(*entry.scene));
            }
        }
    }
}

// ---- reflection (see :components for why this lives here) ----
namespace engine::physics
{
    REFLECT_ENUM(MotionKind, "rtti::engine::physics")
    {
        builder.Value("Static", MotionKind::Static);
        builder.Value("Kinematic", MotionKind::Kinematic);
        builder.Value("Dynamic", MotionKind::Dynamic);
    }

    REFLECT_ENUM(PhysicsLayer, "rtti::engine::physics")
    {
        builder.Value("Static", PhysicsLayer::Static);
        builder.Value("Dynamic", PhysicsLayer::Dynamic);
        builder.Value("Kinematic", PhysicsLayer::Kinematic);
        builder.Value("Trigger", PhysicsLayer::Trigger);
    }

    REFLECT_ENUM(JointKind, "rtti::engine::physics")
    {
        builder.Value("Fixed", JointKind::Fixed);
        builder.Value("Point", JointKind::Point);
        builder.Value("Hinge", JointKind::Hinge);
        builder.Value("Slider", JointKind::Slider);
        builder.Value("Distance", JointKind::Distance);
    }

    REFLECT_ENUM(ShapeKind, "rtti::engine::physics")
    {
        builder.Value("Box", ShapeKind::Box);
        builder.Value("Sphere", ShapeKind::Sphere);
        builder.Value("Capsule", ShapeKind::Capsule);
        builder.Value("Cooked", ShapeKind::Cooked);
        builder.Value("Plane", ShapeKind::Plane);
    }

    REFLECT_VALUE(RigidBodyComponent, "rtti::engine::physics")
    {
        builder.Attribute("displayName", String(u8"Rigid Body"))
            .Attribute("category", String(u8"Physics")).DataVersion(1);
        builder.Property<&RigidBodyComponent::motion>("motion");
        builder.Property<&RigidBodyComponent::layer>("layer");
        builder.Property<&RigidBodyComponent::shape>("shape");
        // Shape-conditional rows (the LightComponent pattern): the inspector shows a
        // dimension only for the ShapeKind that uses it (Box=0 Sphere=1 Capsule=2
        // Cooked=3 Plane=4).
        builder.Property<&RigidBodyComponent::halfExtents>("halfExtents")
            .PropAttribute("visibleWhen", String(u8"shape=0"));
        builder.Property<&RigidBodyComponent::radius>("radius")
            .PropAttribute("visibleWhen", String(u8"shape=1,2"));
        builder.Property<&RigidBodyComponent::halfHeight>("halfHeight")
            .PropAttribute("visibleWhen", String(u8"shape=2"));
        builder.Property<&RigidBodyComponent::planeHalfExtent>("planeHalfExtent")
            .PropAttribute("visibleWhen", String(u8"shape=4"));
        builder.Property<&RigidBodyComponent::friction>("friction");
        builder.Property<&RigidBodyComponent::restitution>("restitution");
        builder.Property<&RigidBodyComponent::linearDamping>("linearDamping");
        builder.Property<&RigidBodyComponent::angularDamping>("angularDamping");
        builder.Property<&RigidBodyComponent::isTrigger>("isTrigger");
        builder.Property<&RigidBodyComponent::collisionGroup>("collisionGroup");
        builder.Property<&RigidBodyComponent::collisionShape>("collisionShape");
        builder.Property<&RigidBodyComponent::material>("material");
        // OPTION 1 (spec Section 12): RigidBodyComponent.of(entity) -> a re-resolving handle.
        builder.Method<&foundation::script::ComponentOf<RigidBodyComponent>, RigidBodyComponent>("of");
    }

    REFLECT_VALUE(ColliderComponent, "rtti::engine::physics")
    {
        builder.Attribute("displayName", String(u8"Collider"))
            .Attribute("category", String(u8"Physics")).DataVersion(1);
        builder.Property<&ColliderComponent::shape>("shape");
        builder.Property<&ColliderComponent::halfExtents>("halfExtents")
            .PropAttribute("visibleWhen", String(u8"shape=0"));
        builder.Property<&ColliderComponent::radius>("radius")
            .PropAttribute("visibleWhen", String(u8"shape=1,2"));
        builder.Property<&ColliderComponent::halfHeight>("halfHeight")
            .PropAttribute("visibleWhen", String(u8"shape=2"));
        builder.Property<&ColliderComponent::planeHalfExtent>("planeHalfExtent")
            .PropAttribute("visibleWhen", String(u8"shape=4"));
        builder.Property<&ColliderComponent::collisionShape>("collisionShape")
            .PropAttribute("visibleWhen", String(u8"shape=3"));
    }

    REFLECT_VALUE(CharacterComponent, "rtti::engine::physics")
    {
        builder.Attribute("displayName", String(u8"Character"))
            .Attribute("category", String(u8"Physics")).DataVersion(1);
        builder.Property<&CharacterComponent::radius>("radius");
        builder.Property<&CharacterComponent::halfHeight>("halfHeight");
        builder.Property<&CharacterComponent::maxSlopeDegrees>("maxSlopeDegrees");
        builder.Property<&CharacterComponent::mass>("mass");
        builder.Property<&CharacterComponent::maxStrength>("maxStrength");
        builder.Property<&CharacterComponent::stepUp>("stepUp");
        builder.Property<&CharacterComponent::stepDown>("stepDown");
        // OPTION 1 (spec Section 12): CharacterComponent.of(entity) -> a re-resolving handle.
        builder.Method<&foundation::script::ComponentOf<CharacterComponent>, CharacterComponent>("of");
        // Per-entity character control (component-data ops - fixes the static facade's first-character
        // limitation): Character.of(entity).move(x, z) / .jump(speed) / .grounded() / .positionY().
        builder.Method<&CharacterComponent::move>("move", {"velocityX", "velocityZ"});
        builder.Method<&CharacterComponent::jump>("jump", {"speed"});
        builder.Method<&CharacterComponent::setPosition>("setPosition", {"x", "y", "z"});
        builder.Method<&CharacterComponent::grounded>("grounded");
        builder.Method<&CharacterComponent::positionX>("positionX");
        builder.Method<&CharacterComponent::positionY>("positionY");
        builder.Method<&CharacterComponent::positionZ>("positionZ");
    }

    REFLECT_VALUE(JointComponent, "rtti::engine::physics")
    {
        builder.Attribute("displayName", String(u8"Joint"))
            .Attribute("category", String(u8"Physics")).DataVersion(1);
        builder.Property<&JointComponent::kind>("kind");
        builder.Property<&JointComponent::targetEntity>("targetEntity");
        builder.Property<&JointComponent::localAnchor>("localAnchor");
        builder.Property<&JointComponent::localAxis>("localAxis");
        builder.Property<&JointComponent::limitMin>("limitMin");
        builder.Property<&JointComponent::limitMax>("limitMax");
        builder.Property<&JointComponent::minDistance>("minDistance");
        builder.Property<&JointComponent::maxDistance>("maxDistance");
        builder.Property<&JointComponent::motorEnabled>("motorEnabled");
        builder.Property<&JointComponent::motorTargetVelocity>("motorTargetVelocity");
        builder.Property<&JointComponent::motorLimit>("motorLimit");
    }

    REFLECT_VALUE(PhysicsSceneSettings, "rtti::engine::physics")
    {
        builder.DataVersion(1);
        builder.Property<&PhysicsSceneSettings::gravity>("gravity");
        builder.Property<&PhysicsSceneSettings::collisionSteps>("collisionSteps");
        builder.Property<&PhysicsSceneSettings::debugDraw>("debugDraw");
    }

    // The scene-bound physics handle: `ScenePhysics.of(scene).rayCast/gravityY/setGravity/...` on
    // THAT scene's world (the explicit-scene replacement for the retired static Physics facade).
    // Reflected with authored parameter names (A6). The `of` factory returns ScenePhysics by value
    // (concrete return type - cross-backend, no ReturnType-override needed).
    REFLECT_VALUE(ScenePhysics, "rtti::engine::physics")
    {
        builder.Method<&ScenePhysics::rayCast>(
            "rayCast", {"fromX", "fromY", "fromZ", "dirX", "dirY", "dirZ", "maxDistance"});
        builder.Method<&ScenePhysics::hitX>("hitX");
        builder.Method<&ScenePhysics::hitY>("hitY");
        builder.Method<&ScenePhysics::hitZ>("hitZ");
        builder.Method<&ScenePhysics::hitNormalX>("hitNormalX");
        builder.Method<&ScenePhysics::hitNormalY>("hitNormalY");
        builder.Method<&ScenePhysics::hitNormalZ>("hitNormalZ");
        builder.Method<&ScenePhysics::hitSurface>("hitSurface");
        builder.Method<&ScenePhysics::rayHitEntity>("rayHitEntity");
        builder.Method<&ScenePhysics::impulseOnHit>("impulseOnHit", {"x", "y", "z"});
        builder.Method<&ScenePhysics::bodyCount>("bodyCount");
        builder.Method<&ScenePhysics::setGravity>("setGravity", {"x", "y", "z"});
        builder.Method<&ScenePhysics::gravityY>("gravityY");
        builder.Method<&ScenePhysics::applyImpulse>("applyImpulse", {"entity", "x", "y", "z"});
        builder.Method<&ScenePhysics::of>("of", {"scene"});
    }

    void RegisterPhysicsScriptFacade()
    {
        RegisterPhysicsComponentReflection(); // ensure component TypeData (incl `of`) is built first

        // Surface the physics COMPONENTS to script (OPTION 1: RigidBodyComponent.of(entity), ...):
        // register them (both backends emit registry types), seed Wren emission roots (reachability),
        // and make their class names import-visible in behavior preludes.
        const core::TypeInfo* components[] = {&core::TypeOf<RigidBodyComponent>(),
                                              &core::TypeOf<CharacterComponent>()};
        for (const core::TypeInfo* component : components)
        {
            GlobalTypeRegistry().Register(*component);
            foundation::script::RegisterExtraScriptRootType(component);
        }
        foundation::script::RegisterExtraFacadeName(u8"RigidBodyComponent");
        foundation::script::RegisterExtraFacadeName(u8"CharacterComponent");

        // The scene-bound physics handle (ScenePhysics.of(scene)): reflect it, register it, seed the
        // Wren emission root (nothing else reaches it), and make the class name prelude-visible.
        RttiRegisterValue_ScenePhysics();
        GlobalTypeRegistry().Register(core::TypeOf<ScenePhysics>());
        foundation::script::RegisterExtraScriptRootType(&core::TypeOf<ScenePhysics>());
        foundation::script::RegisterExtraFacadeName(u8"ScenePhysics");
    }

    void RegisterPhysicsComponentReflection()
    {
        static const bool once = []()
        {
            RttiRegisterEnum_MotionKind();
            RttiRegisterEnum_PhysicsLayer();
            RttiRegisterEnum_ShapeKind();
            RttiRegisterEnum_JointKind();
            RttiRegisterValue_RigidBodyComponent();
            RttiRegisterValue_ColliderComponent();
            RttiRegisterValue_JointComponent();
            RttiRegisterValue_CharacterComponent();
            RttiRegisterValue_PhysicsSceneSettings();
            return true;
        }();
        (void)once;
    }
}
