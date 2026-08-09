// Draconic::PhysicsSubsystem - :components partition.
//
// The authoring components (docs/design/physics.md §3.2): RigidBodyComponent carries the
// body's motion/layer/material properties plus its OWN primitive shape; ColliderComponent
// on DESCENDANT entities adds extra shapes that fold into the nearest ancestor body's
// compound (hierarchy compounding). Runtime fields (body handle, pose double-buffer for
// interpolation) are transient - never serialized.

module;
#include "Core/Prelude.h"

export module draconic.engine.physics:components;

import draconic.core;
import draconic.scene;
import draconic.resource;
import draconic.physics;
import draconic.physics.resource;

using namespace foundation::core;
using namespace foundation::physics;

export namespace engine::physics
{
    struct RigidBodyComponent
    {
        // Authored:
        MotionKind motion = MotionKind::Dynamic;
        PhysicsLayer layer = PhysicsLayer::Dynamic;
        ShapeKind shape = ShapeKind::Box;
        Float3 halfExtents{0.5f, 0.5f, 0.5f};
        f32 radius = 0.5f;
        f32 halfHeight = 0.5f;
        // shape == ShapeKind::Plane: the entity's local XZ plane (+Y solid-below),
        // collidable within +-planeHalfExtent of the entity (static/kinematic only).
        f32 planeHalfExtent = 1000.0f;
        f32 friction = 0.5f;
        f32 restitution = 0.0f;
        f32 linearDamping = 0.05f;
        f32 angularDamping = 0.05f;
        bool isTrigger = false;
        // Designer collision group [0, 32) - pairs collide when the scene's group matrix
        // allows it (PhysicsSceneSettings::groupCollides).
        u8 collisionGroup = 0;
        // shape == ShapeKind::Cooked: the cooked collision-shape resource to use.
        foundation::resource::Ref<CollisionShape> collisionShape;
        // Optional surface override: when set, wins over the inline friction/restitution.
        foundation::resource::Ref<PhysicalMaterial> material;

        // Runtime (transient):
        BodyId body;
        Float3 prevPosition{0, 0, 0};
        Float3 currPosition{0, 0, 0};
        Quaternion prevRotation = Quaternion::Identity;
        Quaternion currRotation = Quaternion::Identity;
    };

    // An extra shape on a descendant entity, folded into the ancestor body's compound at
    // Start (its offset = its transform relative to the body entity, captured then).
    struct ColliderComponent
    {
        ShapeKind shape = ShapeKind::Box;
        Float3 halfExtents{0.5f, 0.5f, 0.5f};
        f32 radius = 0.5f;
        f32 halfHeight = 0.5f;
        f32 planeHalfExtent = 1000.0f;                          // shape == Plane
        foundation::resource::Ref<CollisionShape> collisionShape; // shape == Cooked
    };

    inline void Serialize(ISerializer& ar, RigidBodyComponent& c)
    {
        u8 motion = static_cast<u8>(c.motion);
        u8 layer = static_cast<u8>(c.layer);
        u8 shape = static_cast<u8>(c.shape);
        foundation::core::Serialize(ar, "motion", motion);
        foundation::core::Serialize(ar, "layer", layer);
        foundation::core::Serialize(ar, "shape", shape);
        c.motion = static_cast<MotionKind>(motion);
        c.layer = static_cast<PhysicsLayer>(layer);
        c.shape = static_cast<ShapeKind>(shape);
        foundation::core::Serialize(ar, "halfExtents", c.halfExtents);
        foundation::core::Serialize(ar, "radius", c.radius);
        foundation::core::Serialize(ar, "halfHeight", c.halfHeight);
        foundation::core::Serialize(ar, "planeHalfExtent", c.planeHalfExtent);
        foundation::core::Serialize(ar, "friction", c.friction);
        foundation::core::Serialize(ar, "restitution", c.restitution);
        foundation::core::Serialize(ar, "linearDamping", c.linearDamping);
        foundation::core::Serialize(ar, "angularDamping", c.angularDamping);
        foundation::core::Serialize(ar, "isTrigger", c.isTrigger);
        foundation::core::Serialize(ar, "collisionGroup", c.collisionGroup);
        foundation::core::Serialize(ar, "collisionShape", c.collisionShape);
        foundation::core::Serialize(ar, "material", c.material);
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager,
                                 RigidBodyComponent& c)
    {
        c.collisionShape.Bind(manager);
        c.material.Bind(manager);
    }

    inline void Serialize(ISerializer& ar, ColliderComponent& c)
    {
        u8 shape = static_cast<u8>(c.shape);
        foundation::core::Serialize(ar, "shape", shape);
        c.shape = static_cast<ShapeKind>(shape);
        foundation::core::Serialize(ar, "halfExtents", c.halfExtents);
        foundation::core::Serialize(ar, "radius", c.radius);
        foundation::core::Serialize(ar, "halfHeight", c.halfHeight);
        foundation::core::Serialize(ar, "planeHalfExtent", c.planeHalfExtent);
        foundation::core::Serialize(ar, "collisionShape", c.collisionShape);
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager, ColliderComponent& c)
    {
        c.collisionShape.Bind(manager);
    }

    class RigidBodyComponentManager final
        : public foundation::scene::SerializableComponentManager<RigidBodyComponent>
    {
    public:
        RigidBodyComponentManager()
            : SerializableComponentManager<RigidBodyComponent>(u8"physics.RigidBody")
        {
        }
    };

    class ColliderComponentManager final
        : public foundation::scene::SerializableComponentManager<ColliderComponent>
    {
    public:
        ColliderComponentManager()
            : SerializableComponentManager<ColliderComponent>(u8"physics.Collider")
        {
        }
    };

    // Jolt CharacterVirtual on this entity: a kinematic capsule with slope/step/stair
    // handling that pushes dynamic bodies (maxStrength newtons). The entity transform's
    // POSITION is the capsule CENTER and is physics-owned while simulating (interpolated
    // like dynamic bodies); rotation stays scene-owned (gameplay yaw). Gameplay drives
    // moveVelocity (world space; y ignored while grounded) and one-shot jumpSpeed.
    struct CharacterComponent
    {
        // Authored:
        f32 radius = 0.35f;
        f32 halfHeight = 0.55f; // cylinder half-length (total = 2*(halfHeight+radius))
        f32 maxSlopeDegrees = 50.0f;
        f32 mass = 70.0f;
        f32 maxStrength = 500.0f; // push force cap (Jolt's 100 barely nudges props)
        f32 stepUp = 0.4f;
        f32 stepDown = 0.5f;

        // Runtime input (gameplay/scripts write):
        Float3 moveVelocity{0.0f, 0.0f, 0.0f};
        f32 jumpSpeed = 0.0f; // consumed at the next grounded step
        Float3 teleportTo{0.0f, 0.0f, 0.0f};
        bool teleportPending = false; // consumed (snap) at the next step, then cleared

        // Runtime (transient):
        CharacterId character;
        CharacterGround ground = CharacterGround::InAir;
        Float3 prevPosition{0, 0, 0};
        Float3 currPosition{0, 0, 0};

        // ---- script gameplay surface (Character.of(entity).<op>): pure component-data ops, no
        // world access - the physics tick reads moveVelocity/jumpSpeed and writes ground/currPosition,
        // so per-entity character control works via reflection (fixes the static facade's "first
        // character only" limitation). ----
        void move(f32 velocityX, f32 velocityZ) { moveVelocity = Float3{velocityX, 0.0f, velocityZ}; }
        void jump(f32 speed) { jumpSpeed = speed; }
        // Teleport/respawn: request a hard snap to (x, y, z). The physics tick moves the
        // CharacterVirtual (a live character owns its transform, so a plain entity.setPosition would be
        // overwritten next step) and drops momentum. Applied once, then the request clears.
        void setPosition(f32 x, f32 y, f32 z)
        {
            teleportTo = Float3{x, y, z};
            teleportPending = true;
        }
        [[nodiscard]] bool grounded() const { return ground == CharacterGround::OnGround; }
        [[nodiscard]] f32 positionX() const { return currPosition.x; }
        [[nodiscard]] f32 positionY() const { return currPosition.y; }
        [[nodiscard]] f32 positionZ() const { return currPosition.z; }
    };

    inline void Serialize(ISerializer& ar, CharacterComponent& c)
    {
        foundation::core::Serialize(ar, "radius", c.radius);
        foundation::core::Serialize(ar, "halfHeight", c.halfHeight);
        foundation::core::Serialize(ar, "maxSlopeDegrees", c.maxSlopeDegrees);
        foundation::core::Serialize(ar, "mass", c.mass);
        foundation::core::Serialize(ar, "maxStrength", c.maxStrength);
        foundation::core::Serialize(ar, "stepUp", c.stepUp);
        foundation::core::Serialize(ar, "stepDown", c.stepDown);
    }

    class CharacterComponentManager final
        : public foundation::scene::SerializableComponentManager<CharacterComponent>
    {
    public:
        CharacterComponentManager()
            : SerializableComponentManager<CharacterComponent>(u8"physics.Character")
        {
        }
    };

    // A joint on THIS entity's rigid body. Target resolution: explicit entity guid; nil =
    // the nearest ANCESTOR entity with a rigid body (prefab-safe - guids inside prefab
    // payloads are remapped per instance, so hierarchy is the stable reference); no
    // ancestor body = anchored to the WORLD.
    struct JointComponent
    {
        JointKind kind = JointKind::Fixed;
        Guid targetEntity;                    // nil = nearest ancestor body / world
        Float3 localAnchor{0.0f, 0.0f, 0.0f}; // pivot in THIS entity's space
        Float3 localAxis{0.0f, 1.0f, 0.0f};   // hinge/slider axis in THIS entity's space
        f32 limitMin = 1.0f;                  // min > max = unlimited
        f32 limitMax = -1.0f;
        f32 minDistance = -1.0f; // Distance: negative = starting distance
        f32 maxDistance = -1.0f;
        bool motorEnabled = false;
        f32 motorTargetVelocity = 0.0f; // rad/s (hinge) / m/s (slider)
        f32 motorLimit = 1.0e6f;        // torque / force cap

        // Runtime (transient):
        JointId joint;
    };

    inline void Serialize(ISerializer& ar, JointComponent& c)
    {
        u8 kind = static_cast<u8>(c.kind);
        foundation::core::Serialize(ar, "kind", kind);
        c.kind = static_cast<JointKind>(kind);
        foundation::core::Serialize(ar, "targetEntity", c.targetEntity);
        foundation::core::Serialize(ar, "localAnchor", c.localAnchor);
        foundation::core::Serialize(ar, "localAxis", c.localAxis);
        foundation::core::Serialize(ar, "limitMin", c.limitMin);
        foundation::core::Serialize(ar, "limitMax", c.limitMax);
        foundation::core::Serialize(ar, "minDistance", c.minDistance);
        foundation::core::Serialize(ar, "maxDistance", c.maxDistance);
        foundation::core::Serialize(ar, "motorEnabled", c.motorEnabled);
        foundation::core::Serialize(ar, "motorTargetVelocity", c.motorTargetVelocity);
        foundation::core::Serialize(ar, "motorLimit", c.motorLimit);
    }

    class JointComponentManager final
        : public foundation::scene::SerializableComponentManager<JointComponent>
    {
    public:
        JointComponentManager() : SerializableComponentManager<JointComponent>(u8"physics.Joint") {}
    };

    // Scene-level physics settings (the editor's scene inspector edits the reflected type;
    // SerializeScene persists it like the environment block).
    struct PhysicsSceneSettings
    {
        Float3 gravity{0.0f, -9.81f, 0.0f};
        i32 collisionSteps = 1;
        bool debugDraw = false;
        // Designer collision groups: names give the matrix rows meaning in the editor
        // (index = group; missing names show as "Group N"). Matrix rows beyond the
        // array's size default to collide-with-everything.
        Array<String> groupNames;
        Array<u32> groupCollides;
    };

    // ONE serializer for the settings block - the scene system persists through it, and
    // the editor's collision-matrix editor round-trips edited COPIES through the same
    // code (the whole-block undo command replays these exact bytes).
    inline void SerializePhysicsSceneSettings(ISerializer& ar, PhysicsSceneSettings& settings)
    {
        foundation::core::Serialize(ar, "gravity", settings.gravity);
        foundation::core::Serialize(ar, "collisionSteps", settings.collisionSteps);
        foundation::core::Serialize(ar, "debugDraw", settings.debugDraw);
        foundation::core::Serialize(ar, "groupNames", settings.groupNames);
        foundation::core::Serialize(ar, "groupCollides", settings.groupCollides);
    }

    // Defined in SubsystemImpl.cpp: the DRACONIC_REFLECT_* bodies live there because
    // GCC's module serializer emits an unreadable gcm cluster when they sit in this
    // partition (the -fno-module-lazy eager load then fails for every consumer).
    void RegisterPhysicsComponentReflection();
}
