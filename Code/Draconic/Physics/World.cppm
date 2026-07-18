// Draconic::Physics - :world partition.
//
// PhysicsWorld: the Jolt-backed rigid-body world (docs/design/physics.md). Jolt is the
// COMMITTED backend - no abstraction layer - but JPH types never appear here AT ALL:
// this interface unit is Jolt-free (all Jolt contact lives in WorldImpl.cpp, a module
// IMPLEMENTATION unit) both for API hygiene and because GCC's C++20-modules serializer
// chokes on Jolt's header mass inside an interface unit's global fragment. Bodies are
// BodyId handles; shapes/queries/events speak engine types; the per-body u64 user data
// carries the scene-entity reverse map. One world per SCENE; stepping is driven from the
// engine's fixed-update lane.
//
// Layers (§3.3): a FIXED semantic table - Static / Dynamic / Kinematic / Trigger - with
// a hard-coded collision matrix (ez-style named matrix; triggers are Jolt sensors:
// overlap events, no response).

module;
#include "Core/Prelude.h"

export module draconic.physics:world;

import draconic.core;

using namespace draconic::core;

export namespace draconic::physics
{
    // ---- public vocabulary (engine types only) ----

    enum class PhysicsLayer : u8
    {
        Static = 0,     // immovable level geometry
        Dynamic,        // simulated bodies
        Kinematic,      // scene-driven movers (platforms)
        Trigger,        // sensors: overlap events, no collision response
        Count,
    };

    enum class MotionKind : u8 { Static, Kinematic, Dynamic };

    enum class ShapeKind : u8 { Box, Sphere, Capsule };

    // One primitive shape (a body carries one or more; >1 = compound).
    struct ShapeDesc
    {
        ShapeKind kind = ShapeKind::Box;
        Float3 halfExtents{ 0.5f, 0.5f, 0.5f };  // Box
        f32 radius = 0.5f;                        // Sphere / Capsule
        f32 halfHeight = 0.5f;                    // Capsule (cylinder half-length)
        Float3 localPosition{ 0.0f, 0.0f, 0.0f }; // compound child placement
        Quaternion localRotation = Quaternion::Identity;
    };

    struct BodyDesc
    {
        MotionKind motion = MotionKind::Dynamic;
        PhysicsLayer layer = PhysicsLayer::Dynamic;
        Array<ShapeDesc> shapes;                  // >= 1; several = compound
        Float3 position{ 0.0f, 0.0f, 0.0f };
        Quaternion rotation = Quaternion::Identity;
        f32 density = 1000.0f;                    // kg/m^3 (Jolt convention)
        f32 friction = 0.5f;
        f32 restitution = 0.0f;
        f32 linearDamping = 0.05f;
        f32 angularDamping = 0.05f;
        bool isTrigger = false;                   // sensor (forces layer Trigger)
        u64 userData = 0;                         // scene-entity reverse map (guid low bits)
    };

    struct BodyId
    {
        u32 value = 0xFFFFFFFFu;
        [[nodiscard]] bool IsValid() const noexcept { return value != 0xFFFFFFFFu; }
        friend bool operator==(BodyId a, BodyId b) noexcept { return a.value == b.value; }
    };

    struct RayHit
    {
        BodyId body;
        u64 userData = 0;
        Float3 position{ 0, 0, 0 };
        Float3 normal{ 0, 0, 0 };
        f32 fraction = 1.0f;
    };

    enum class ContactKind : u8 { Begin, End, TriggerEnter, TriggerExit };

    struct ContactEvent
    {
        ContactKind kind = ContactKind::Begin;
        BodyId bodyA;
        BodyId bodyB;
        u64 userA = 0;
        u64 userB = 0;
    };

    struct PhysicsWorldSettings
    {
        Float3 gravity{ 0.0f, -9.81f, 0.0f };
        u32 maxBodies = 4096;
        u32 maxBodyPairs = 4096;
        u32 maxContactConstraints = 2048;
    };

    class PhysicsWorld
    {
    public:
        explicit PhysicsWorld(const PhysicsWorldSettings& settings = {});
        ~PhysicsWorld();
        PhysicsWorld(const PhysicsWorld&) = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;

        void SetGravity(Float3 gravity);
        [[nodiscard]] Float3 Gravity() const;

        // ---- bodies ----
        [[nodiscard]] BodyId CreateBody(const BodyDesc& desc);
        void DestroyBody(BodyId id);
        [[nodiscard]] usize BodyCount() const;

        // ---- stepping ----
        /// One fixed step. Contact events buffered during the step are available from
        /// DrainContacts() afterwards.
        void Step(f32 deltaTime, i32 collisionSteps = 1);

        // ---- transforms & motion ----
        void GetBodyTransform(BodyId id, Float3& outPosition, Quaternion& outRotation) const;
        /// Teleport: snaps the body (velocities untouched). Dynamic bodies mid-sim should
        /// use this, never per-frame scene writes (transform ownership, §3.2).
        void SetBodyTransform(BodyId id, Float3 position, Quaternion rotation);
        /// Velocity-correct kinematic move over `deltaTime` (the fixed step).
        void MoveKinematic(BodyId id, Float3 position, Quaternion rotation, f32 deltaTime);
        void SetLinearVelocity(BodyId id, Float3 velocity);
        [[nodiscard]] Float3 LinearVelocity(BodyId id) const;
        void AddImpulse(BodyId id, Float3 impulse);
        void AddForce(BodyId id, Float3 force);
        [[nodiscard]] bool IsActive(BodyId id) const;
        [[nodiscard]] u64 UserData(BodyId id) const;

        // ---- queries ----
        [[nodiscard]] bool RayCast(Float3 from, Float3 direction, f32 maxDistance, RayHit& out) const;
        /// Bodies whose shapes contain `point` (triggers included).
        void QueryPoint(Float3 point, Array<BodyId>& out) const;

        // ---- contact events ----
        /// Moves the events buffered since the last drain (worker-thread listeners append
        /// under a mutex; the fixed-step driver drains on the main thread).
        void DrainContacts(Array<ContactEvent>& out);

    private:
        struct Impl;
        UniquePtr<Impl> m_impl;
    };
}
