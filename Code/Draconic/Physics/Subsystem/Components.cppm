// Draconic::PhysicsSubsystem - :components partition.
//
// The authoring components (docs/design/physics.md §3.2): RigidBodyComponent carries the
// body's motion/layer/material properties plus its OWN primitive shape; ColliderComponent
// on DESCENDANT entities adds extra shapes that fold into the nearest ancestor body's
// compound (hierarchy compounding). Runtime fields (body handle, pose double-buffer for
// interpolation) are transient - never serialized.

module;
#include "Core/Prelude.h"

export module draconic.physics.subsystem:components;

import draconic.core;
import draconic.scene;
import draconic.physics;

using namespace draconic::core;

export namespace draconic::physics
{
    struct RigidBodyComponent
    {
        // Authored:
        MotionKind motion = MotionKind::Dynamic;
        PhysicsLayer layer = PhysicsLayer::Dynamic;
        ShapeKind shape = ShapeKind::Box;
        Float3 halfExtents{ 0.5f, 0.5f, 0.5f };
        f32 radius = 0.5f;
        f32 halfHeight = 0.5f;
        f32 friction = 0.5f;
        f32 restitution = 0.0f;
        f32 linearDamping = 0.05f;
        f32 angularDamping = 0.05f;
        bool isTrigger = false;

        // Runtime (transient):
        BodyId body;
        Float3 prevPosition{ 0, 0, 0 };
        Float3 currPosition{ 0, 0, 0 };
        Quaternion prevRotation = Quaternion::Identity;
        Quaternion currRotation = Quaternion::Identity;
    };

    // An extra shape on a descendant entity, folded into the ancestor body's compound at
    // Start (its offset = its transform relative to the body entity, captured then).
    struct ColliderComponent
    {
        ShapeKind shape = ShapeKind::Box;
        Float3 halfExtents{ 0.5f, 0.5f, 0.5f };
        f32 radius = 0.5f;
        f32 halfHeight = 0.5f;
    };

    inline void Serialize(ISerializer& ar, RigidBodyComponent& c)
    {
        u8 motion = static_cast<u8>(c.motion);
        u8 layer = static_cast<u8>(c.layer);
        u8 shape = static_cast<u8>(c.shape);
        draconic::core::Serialize(ar, "motion", motion);
        draconic::core::Serialize(ar, "layer", layer);
        draconic::core::Serialize(ar, "shape", shape);
        c.motion = static_cast<MotionKind>(motion);
        c.layer = static_cast<PhysicsLayer>(layer);
        c.shape = static_cast<ShapeKind>(shape);
        draconic::core::Serialize(ar, "halfExtents", c.halfExtents);
        draconic::core::Serialize(ar, "radius", c.radius);
        draconic::core::Serialize(ar, "halfHeight", c.halfHeight);
        draconic::core::Serialize(ar, "friction", c.friction);
        draconic::core::Serialize(ar, "restitution", c.restitution);
        draconic::core::Serialize(ar, "linearDamping", c.linearDamping);
        draconic::core::Serialize(ar, "angularDamping", c.angularDamping);
        draconic::core::Serialize(ar, "isTrigger", c.isTrigger);
    }

    inline void Serialize(ISerializer& ar, ColliderComponent& c)
    {
        u8 shape = static_cast<u8>(c.shape);
        draconic::core::Serialize(ar, "shape", shape);
        c.shape = static_cast<ShapeKind>(shape);
        draconic::core::Serialize(ar, "halfExtents", c.halfExtents);
        draconic::core::Serialize(ar, "radius", c.radius);
        draconic::core::Serialize(ar, "halfHeight", c.halfHeight);
    }

    class RigidBodyComponentManager final
        : public draconic::scene::SerializableComponentManager<RigidBodyComponent>
    {
    public:
        RigidBodyComponentManager()
            : SerializableComponentManager<RigidBodyComponent>(u8"physics.RigidBody") {}
    };

    class ColliderComponentManager final
        : public draconic::scene::SerializableComponentManager<ColliderComponent>
    {
    public:
        ColliderComponentManager()
            : SerializableComponentManager<ColliderComponent>(u8"physics.Collider") {}
    };

    // Scene-level physics settings (the editor's scene inspector edits the reflected type;
    // SerializeScene persists it like the environment block).
    struct PhysicsSceneSettings
    {
        Float3 gravity{ 0.0f, -9.81f, 0.0f };
        i32 collisionSteps = 1;
        bool debugDraw = false;
    };

    // Defined in SubsystemImpl.cpp: the DRACONIC_REFLECT_* bodies live there because
    // GCC's module serializer emits an unreadable gcm cluster when they sit in this
    // partition (the -fno-module-lazy eager load then fails for every consumer).
    void RegisterPhysicsComponentReflection();
}

