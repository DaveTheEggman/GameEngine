// Draconic::PhysicsSubsystem - implementation unit: the render-frame drive (interpolation
// + debug wireframes) and the component reflection bodies. Both live OUTSIDE the interface
// for GCC: heavy render imports stay out of the interface's module graph, and the
// DRACONIC_REFLECT_* macros in the :components partition made GCC emit a gcm with an
// unreadable cluster (every -fno-module-lazy consumer then failed to import the module).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"
#include <cmath>

module draconic.physics.subsystem;

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.physics;
import draconic.render;
import draconic.render.subsystem;

using namespace draconic::core;

namespace draconic::physics
{
    namespace
    {
        // Body wireframes: green = awake dynamic, grey = sleeping, blue = static/kinematic,
        // yellow = trigger.
        void DrawPhysicsDebug(PhysicsSceneSystem& system, draconic::render::debug::DebugDraw& draw)
        {
            PhysicsWorld* world = system.World();
            draconic::scene::Scene* scene = system.ScenePtr();
            if (world == nullptr || scene == nullptr) { return; }
            auto* bodies = scene->GetSystem<RigidBodyComponentManager>();
            if (bodies == nullptr) { return; }
            bodies->ForEach([&](RigidBodyComponent& c, draconic::scene::EntityHandle) {
                if (!c.body.IsValid()) { return; }
                Float3 position;
                Quaternion rotation;
                world->GetBodyTransform(c.body, position, rotation);
                const Color color = c.isTrigger ? Color{ 1.0f, 0.8f, 0.2f, 1.0f }
                                  : c.motion == MotionKind::Dynamic
                                      ? (world->IsActive(c.body) ? Color{ 0.3f, 1.0f, 0.4f, 1.0f }
                                                                 : Color{ 0.5f, 0.6f, 0.5f, 1.0f })
                                      : Color{ 0.4f, 0.6f, 1.0f, 1.0f };
                const Float4x4 worldMatrix =
                    Transform{ position, rotation, Float3{ 1, 1, 1 } }.ToMatrix();
                switch (c.shape)
                {
                    case ShapeKind::Box:
                        draw.DrawTransformedBox(
                            Float3{ -c.halfExtents.x, -c.halfExtents.y, -c.halfExtents.z },
                            c.halfExtents, worldMatrix, color);
                        break;
                    case ShapeKind::Sphere:
                        draw.DrawWireSphere(position, c.radius, color);
                        break;
                    case ShapeKind::Capsule:
                        draw.DrawWireSphere(position, c.radius, color);
                        draw.DrawTransformedBox(
                            Float3{ -c.radius, -(c.halfHeight + c.radius), -c.radius },
                            Float3{ c.radius, c.halfHeight + c.radius, c.radius },
                            worldMatrix, color);
                        break;
                }
            });
        }
    }

    void PhysicsSubsystem::Update(f32)
    {
        draconic::runtime::Context* context = GetContext();
        if (context == nullptr) { return; }
        const f32 alpha = context->FixedAlpha();
        auto* render = context->GetSubsystem<draconic::render::RenderSubsystem>();
        for (const SceneEntry& entry : Systems())
        {
            entry.system->ApplyInterpolation(alpha);
            if (render != nullptr && entry.system->Settings().debugDraw)
            {
                DrawPhysicsDebug(*entry.system, render->DebugScene(*entry.scene));
            }
        }
    }
}

// ---- reflection (see :components for why this lives here) ----
namespace draconic::physics
{
    DRACONIC_REFLECT_ENUM(MotionKind, "draconic::physics")
    {
        builder.Value("Static", MotionKind::Static);
        builder.Value("Kinematic", MotionKind::Kinematic);
        builder.Value("Dynamic", MotionKind::Dynamic);
    }

    DRACONIC_REFLECT_ENUM(PhysicsLayer, "draconic::physics")
    {
        builder.Value("Static", PhysicsLayer::Static);
        builder.Value("Dynamic", PhysicsLayer::Dynamic);
        builder.Value("Kinematic", PhysicsLayer::Kinematic);
        builder.Value("Trigger", PhysicsLayer::Trigger);
    }

    DRACONIC_REFLECT_ENUM(ShapeKind, "draconic::physics")
    {
        builder.Value("Box", ShapeKind::Box);
        builder.Value("Sphere", ShapeKind::Sphere);
        builder.Value("Capsule", ShapeKind::Capsule);
    }

    DRACONIC_REFLECT_VALUE(RigidBodyComponent, "draconic::physics")
    {
        builder.DataVersion(1);
        builder.Property<&RigidBodyComponent::motion>("motion");
        builder.Property<&RigidBodyComponent::layer>("layer");
        builder.Property<&RigidBodyComponent::shape>("shape");
        builder.Property<&RigidBodyComponent::halfExtents>("halfExtents");
        builder.Property<&RigidBodyComponent::radius>("radius");
        builder.Property<&RigidBodyComponent::halfHeight>("halfHeight");
        builder.Property<&RigidBodyComponent::friction>("friction");
        builder.Property<&RigidBodyComponent::restitution>("restitution");
        builder.Property<&RigidBodyComponent::linearDamping>("linearDamping");
        builder.Property<&RigidBodyComponent::angularDamping>("angularDamping");
        builder.Property<&RigidBodyComponent::isTrigger>("isTrigger");
    }

    DRACONIC_REFLECT_VALUE(ColliderComponent, "draconic::physics")
    {
        builder.DataVersion(1);
        builder.Property<&ColliderComponent::shape>("shape");
        builder.Property<&ColliderComponent::halfExtents>("halfExtents");
        builder.Property<&ColliderComponent::radius>("radius");
        builder.Property<&ColliderComponent::halfHeight>("halfHeight");
    }

    DRACONIC_REFLECT_VALUE(PhysicsSceneSettings, "draconic::physics")
    {
        builder.DataVersion(1);
        builder.Property<&PhysicsSceneSettings::gravity>("gravity");
        builder.Property<&PhysicsSceneSettings::collisionSteps>("collisionSteps");
        builder.Property<&PhysicsSceneSettings::debugDraw>("debugDraw");
    }

    void RegisterPhysicsComponentReflection()
    {
        static const bool once = []() {
            DraconicRegisterEnum_MotionKind();
            DraconicRegisterEnum_PhysicsLayer();
            DraconicRegisterEnum_ShapeKind();
            DraconicRegisterValue_RigidBodyComponent();
            DraconicRegisterValue_ColliderComponent();
            DraconicRegisterValue_PhysicsSceneSettings();
            return true;
        }();
        (void)once;
    }
}
