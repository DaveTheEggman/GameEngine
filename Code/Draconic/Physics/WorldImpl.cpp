// Draconic::Physics - PhysicsWorld implementation (module IMPLEMENTATION unit).
//
// ALL Jolt contact lives here: the interface stays JPH-free (API hygiene + GCC's module
// serializer cannot digest Jolt's headers inside an interface unit's global fragment).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Core/StreamIn.h>
#include <Jolt/Core/StreamOut.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <atomic>
#include <cstring>

module draconic.physics;

import draconic.core;

using namespace draconic::core;

namespace draconic::physics
{
    // ---- layers ----
    namespace layers
    {
        constexpr JPH::ObjectLayer kStatic    = 0;
        constexpr JPH::ObjectLayer kTrigger   = 3;

        constexpr JPH::BroadPhaseLayer kBpStatic{ 0 };
        constexpr JPH::BroadPhaseLayer kBpMoving{ 1 };
        constexpr JPH::uint kBpCount = 2;

        [[nodiscard]] inline JPH::ObjectLayer From(PhysicsLayer layer)
        {
            return static_cast<JPH::ObjectLayer>(layer);
        }

        // The hard-coded matrix: statics never pair with statics; triggers sense
        // everything that moves; everything else collides.
        [[nodiscard]] inline bool Collides(JPH::ObjectLayer a, JPH::ObjectLayer b)
        {
            if (a == kStatic && b == kStatic) { return false; }
            if (a == kTrigger && b == kTrigger) { return false; }
            if ((a == kTrigger && b == kStatic) || (a == kStatic && b == kTrigger)) { return false; }
            return true;
        }
    }

    namespace
    {
        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
        public:
            [[nodiscard]] JPH::uint GetNumBroadPhaseLayers() const override { return layers::kBpCount; }
            [[nodiscard]] JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return layer == layers::kStatic ? layers::kBpStatic : layers::kBpMoving;
            }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            [[nodiscard]] const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "bp"; }
#endif
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
            {
                if (layer == layers::kStatic) { return bp == layers::kBpMoving; }
                return true;
            }
        };

        class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            [[nodiscard]] bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                return layers::Collides(a, b);
            }
        };

        [[nodiscard]] JPH::Vec3 ToJph(Float3 v) { return JPH::Vec3(v.x, v.y, v.z); }
        [[nodiscard]] JPH::Quat ToJph(Quaternion q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
        [[nodiscard]] Float3 FromJph(JPH::Vec3 v) { return Float3{ v.GetX(), v.GetY(), v.GetZ() }; }
        [[nodiscard]] Quaternion FromJph(JPH::Quat q)
        {
            return Quaternion{ q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
        }

        // Process-wide Jolt bring-up (allocator/factory/types), refcounted across worlds.
        std::atomic<int> g_joltUsers{ 0 };
        void AcquireJolt()
        {
            if (g_joltUsers.fetch_add(1) == 0)
            {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
        }
        void ReleaseJolt()
        {
            if (g_joltUsers.fetch_sub(1) == 1)
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }

        // Cooked blobs are Jolt binary shape state; these adapters bridge it to Array<byte>.
        struct BlobOut final : JPH::StreamOut
        {
            Array<byte>& blob;
            explicit BlobOut(Array<byte>& b) : blob(b) {}
            void WriteBytes(const void* data, size_t count) override
            {
                const usize offset = blob.Size();
                blob.Resize(offset + count);
                std::memcpy(blob.Data() + offset, data, count);
            }
            [[nodiscard]] bool IsFailed() const override { return false; }
        };

        struct BlobIn final : JPH::StreamIn
        {
            Span<const byte> blob;
            usize cursor = 0;
            bool failed = false;
            explicit BlobIn(Span<const byte> b) : blob(b) {}
            void ReadBytes(void* out, size_t count) override
            {
                if (cursor + count > blob.Size()) { failed = true; return; }
                std::memcpy(out, blob.Data() + cursor, count);
                cursor += count;
            }
            // istream semantics: EOF only trips when a read runs PAST the end - Jolt
            // checks IsEOF() after a fully-consumed successful restore.
            [[nodiscard]] bool IsEOF() const override { return failed; }
            [[nodiscard]] bool IsFailed() const override { return failed; }
        };

        [[nodiscard]] JPH::Ref<JPH::Shape> RestoreCooked(Span<const byte> blob)
        {
            // Jolt indexes its construct table with the leading subtype byte UNVALIDATED -
            // reject out-of-range values before handing over a corrupt/foreign blob.
            if (blob.IsEmpty()
                || static_cast<JPH::uint>(blob[0]) >= JPH::NumSubShapeTypes) { return {}; }
            BlobIn in(blob);
            JPH::Shape::ShapeResult result = JPH::Shape::sRestoreFromBinaryState(in);
            if (in.IsFailed() || !result.IsValid()) { return {}; }
            return result.Get();
        }

        [[nodiscard]] JPH::Ref<JPH::Shape> BuildOne(const ShapeDesc& desc, f32 density)
        {
            JPH::Ref<JPH::Shape> shape;
            switch (desc.kind)
            {
                case ShapeKind::Box:
                    shape = new JPH::BoxShape(ToJph(desc.halfExtents));
                    break;
                case ShapeKind::Sphere:
                    shape = new JPH::SphereShape(desc.radius);
                    break;
                case ShapeKind::Capsule:
                    shape = new JPH::CapsuleShape(desc.halfHeight, desc.radius);
                    break;
                case ShapeKind::Cooked:
                    shape = RestoreCooked(desc.cooked);
                    break;
                case ShapeKind::Plane:
                    shape = new JPH::PlaneShape(
                        JPH::Plane(ToJph(desc.planeNormal).Normalized(), desc.planeDistance),
                        nullptr, desc.planeHalfExtent);
                    break;
            }
            // Density drives CalculateMassAndInertia (kg/m^3); only convex shapes carry it.
            if (shape != nullptr && shape->GetType() == JPH::EShapeType::Convex && density > 0.0f)
            {
                static_cast<JPH::ConvexShape*>(shape.GetPtr())->SetDensity(density);
            }
            if (shape != nullptr
                && (desc.scale.x != 1.0f || desc.scale.y != 1.0f || desc.scale.z != 1.0f))
            {
                shape = new JPH::ScaledShape(shape, ToJph(desc.scale));
            }
            return shape;
        }

        [[nodiscard]] JPH::Ref<JPH::Shape> BuildShape(const BodyDesc& desc)
        {
            if (desc.shapes.IsEmpty()) { return nullptr; }
            if (desc.shapes.Size() == 1
                && desc.shapes[0].localPosition.x == 0.0f
                && desc.shapes[0].localPosition.y == 0.0f
                && desc.shapes[0].localPosition.z == 0.0f)
            {
                return BuildOne(desc.shapes[0], desc.density);
            }
            JPH::StaticCompoundShapeSettings compound;
            for (const ShapeDesc& child : desc.shapes)
            {
                JPH::Ref<JPH::Shape> shape = BuildOne(child, desc.density);
                if (shape == nullptr) { return nullptr; }
                compound.AddShape(ToJph(child.localPosition), ToJph(child.localRotation), shape);
            }
            JPH::Shape::ShapeResult result = compound.Create();
            return result.IsValid() ? result.Get() : JPH::Ref<JPH::Shape>{};
        }

        struct ContactBuffer final : JPH::ContactListener
        {
            Mutex mutex;
            Array<ContactEvent> events;

            void OnContactAdded(const JPH::Body& a, const JPH::Body& b,
                                const JPH::ContactManifold&, JPH::ContactSettings&) override
            {
                Push(a, b, a.IsSensor() || b.IsSensor() ? ContactKind::TriggerEnter
                                                        : ContactKind::Begin);
            }
            void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
            {
                ContactEvent e;
                e.kind = ContactKind::End;
                e.bodyA = BodyId{ pair.GetBody1ID().GetIndexAndSequenceNumber() };
                e.bodyB = BodyId{ pair.GetBody2ID().GetIndexAndSequenceNumber() };
                ScopedLock lock(mutex);
                events.PushBack(e);
            }

        private:
            void Push(const JPH::Body& a, const JPH::Body& b, ContactKind kind)
            {
                ContactEvent e;
                e.kind = kind;
                e.bodyA = BodyId{ a.GetID().GetIndexAndSequenceNumber() };
                e.bodyB = BodyId{ b.GetID().GetIndexAndSequenceNumber() };
                e.userA = a.GetUserData();
                e.userB = b.GetUserData();
                ScopedLock lock(mutex);
                events.PushBack(e);
            }
        };
    }

    bool CookConvexHull(Span<const Float3> points, Array<byte>& outBlob, f32 hullTolerance)
    {
        if (points.Size() < 4) { return false; }
        AcquireJolt();   // Factory/type registry must exist for shape construction
        JPH::Array<JPH::Vec3> hull;
        hull.reserve(points.Size());
        for (const Float3& p : points) { hull.push_back(ToJph(p)); }
        JPH::ConvexHullShapeSettings settings(hull);
        settings.mHullTolerance = hullTolerance;
        const JPH::Shape::ShapeResult result = settings.Create();
        bool ok = false;
        if (result.IsValid())
        {
            BlobOut out(outBlob);
            result.Get()->SaveBinaryState(out);
            ok = true;
        }
        ReleaseJolt();
        return ok;
    }

    bool CookTriangleMesh(Span<const Float3> positions, Span<const u32> indices,
                          Span<const u32> triangleMaterialSlots, Array<byte>& outBlob)
    {
        if (positions.IsEmpty() || indices.IsEmpty() || indices.Size() % 3 != 0) { return false; }
        const usize triangleCount = indices.Size() / 3;
        if (!triangleMaterialSlots.IsEmpty() && triangleMaterialSlots.Size() != triangleCount)
        {
            return false;
        }
        AcquireJolt();
        JPH::VertexList vertices;
        vertices.reserve(positions.Size());
        for (const Float3& p : positions) { vertices.push_back(JPH::Float3(p.x, p.y, p.z)); }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(triangleCount);
        for (usize t = 0; t < triangleCount; ++t)
        {
            // Material slot rides in the per-triangle USER DATA (RayHit::surface); Jolt's
            // own material index stays 0 (we don't use JPH::PhysicsMaterial).
            triangles.push_back(JPH::IndexedTriangle(
                indices[t * 3 + 0], indices[t * 3 + 1], indices[t * 3 + 2], 0,
                triangleMaterialSlots.IsEmpty() ? 0 : triangleMaterialSlots[t]));
        }
        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        settings.mPerTriangleUserData = true;
        const JPH::Shape::ShapeResult result = settings.Create();
        bool ok = false;
        if (result.IsValid())
        {
            BlobOut out(outBlob);
            result.Get()->SaveBinaryState(out);
            ok = true;
        }
        ReleaseJolt();
        return ok;
    }

    bool ExtractShapeTriangles(Span<const byte> blob, Array<Float3>& outTriangles)
    {
        AcquireJolt();
        JPH::Ref<JPH::Shape> shape = RestoreCooked(blob);
        bool ok = false;
        if (shape != nullptr)
        {
            JPH::Shape::GetTrianglesContext context;
            shape->GetTrianglesStart(context, JPH::AABox::sBiggest(), JPH::Vec3::sZero(),
                                     JPH::Quat::sIdentity(), JPH::Vec3::sOne());
            JPH::Float3 buffer[3 * JPH::Shape::cGetTrianglesMinTrianglesRequested];
            for (;;)
            {
                const int count = shape->GetTrianglesNext(
                    context, JPH::Shape::cGetTrianglesMinTrianglesRequested, buffer);
                if (count <= 0) { break; }
                for (int v = 0; v < count * 3; ++v)
                {
                    outTriangles.PushBack(Float3{ buffer[v].x, buffer[v].y, buffer[v].z });
                }
            }
            ok = !outTriangles.IsEmpty();
        }
        ReleaseJolt();
        return ok;
    }

    struct PhysicsWorld::Impl
    {
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairFilter pairFilter;
        ContactBuffer contacts;
        UniquePtr<JPH::TempAllocatorImpl> tempAllocator;
        UniquePtr<JPH::JobSystemThreadPool> jobSystem;
        UniquePtr<JPH::PhysicsSystem> system;
    };

    PhysicsWorld::PhysicsWorld(const PhysicsWorldSettings& settings)
    {
        AcquireJolt();
        m_impl = MakeUnique<Impl>(DefaultAllocator());
        m_impl->tempAllocator = MakeUnique<JPH::TempAllocatorImpl>(DefaultAllocator(),
                                                                   10 * 1024 * 1024);
        m_impl->jobSystem = MakeUnique<JPH::JobSystemThreadPool>(DefaultAllocator(),
            JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            static_cast<int>(JPH::thread::hardware_concurrency()) - 1);
        m_impl->system = MakeUnique<JPH::PhysicsSystem>(DefaultAllocator());
        m_impl->system->Init(settings.maxBodies, 0, settings.maxBodyPairs,
                             settings.maxContactConstraints,
                             m_impl->broadPhaseLayers, m_impl->objectVsBroadPhase,
                             m_impl->pairFilter);
        m_impl->system->SetGravity(ToJph(settings.gravity));
        m_impl->system->SetContactListener(&m_impl->contacts);
    }

    PhysicsWorld::~PhysicsWorld()
    {
        m_impl = nullptr;
        ReleaseJolt();
    }

    void PhysicsWorld::SetGravity(Float3 gravity) { m_impl->system->SetGravity(ToJph(gravity)); }
    Float3 PhysicsWorld::Gravity() const { return FromJph(m_impl->system->GetGravity()); }

    BodyId PhysicsWorld::CreateBody(const BodyDesc& desc)
    {
        JPH::Ref<JPH::Shape> shape = BuildShape(desc);
        if (shape == nullptr) { return BodyId{}; }

        const PhysicsLayer layer = desc.isTrigger ? PhysicsLayer::Trigger : desc.layer;
        const JPH::EMotionType motion =
            desc.motion == MotionKind::Static ? JPH::EMotionType::Static
            : desc.motion == MotionKind::Kinematic ? JPH::EMotionType::Kinematic
                                                   : JPH::EMotionType::Dynamic;
        JPH::BodyCreationSettings settings(shape, ToJph(desc.position), ToJph(desc.rotation),
                                           motion, layers::From(layer));
        settings.mFriction = desc.friction;
        settings.mRestitution = desc.restitution;
        settings.mLinearDamping = desc.linearDamping;
        settings.mAngularDamping = desc.angularDamping;
        settings.mIsSensor = desc.isTrigger;
        settings.mUserData = desc.userData;
        settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateMassAndInertia;

        JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
        const JPH::BodyID id = bodies.CreateAndAddBody(
            settings, desc.motion == MotionKind::Static ? JPH::EActivation::DontActivate
                                                        : JPH::EActivation::Activate);
        return id.IsInvalid() ? BodyId{} : BodyId{ id.GetIndexAndSequenceNumber() };
    }

    void PhysicsWorld::DestroyBody(BodyId id)
    {
        if (!id.IsValid()) { return; }
        JPH::BodyInterface& bodies = m_impl->system->GetBodyInterface();
        const JPH::BodyID jolt(id.value);
        bodies.RemoveBody(jolt);
        bodies.DestroyBody(jolt);
    }

    usize PhysicsWorld::BodyCount() const { return m_impl->system->GetNumBodies(); }

    void PhysicsWorld::Step(f32 deltaTime, i32 collisionSteps)
    {
        m_impl->system->Update(deltaTime, collisionSteps, m_impl->tempAllocator.Get(),
                               m_impl->jobSystem.Get());
    }

    void PhysicsWorld::GetBodyTransform(BodyId id, Float3& outPosition, Quaternion& outRotation) const
    {
        const JPH::BodyID jolt(id.value);
        JPH::RVec3 position;
        JPH::Quat rotation;
        m_impl->system->GetBodyInterface().GetPositionAndRotation(jolt, position, rotation);
        outPosition = FromJph(position);
        outRotation = FromJph(rotation);
    }

    void PhysicsWorld::SetBodyTransform(BodyId id, Float3 position, Quaternion rotation)
    {
        m_impl->system->GetBodyInterface().SetPositionAndRotation(
            JPH::BodyID(id.value), ToJph(position), ToJph(rotation), JPH::EActivation::Activate);
    }

    void PhysicsWorld::MoveKinematic(BodyId id, Float3 position, Quaternion rotation, f32 deltaTime)
    {
        m_impl->system->GetBodyInterface().MoveKinematic(
            JPH::BodyID(id.value), ToJph(position), ToJph(rotation), deltaTime);
    }

    void PhysicsWorld::SetLinearVelocity(BodyId id, Float3 velocity)
    {
        m_impl->system->GetBodyInterface().SetLinearVelocity(JPH::BodyID(id.value), ToJph(velocity));
    }

    Float3 PhysicsWorld::LinearVelocity(BodyId id) const
    {
        return FromJph(m_impl->system->GetBodyInterface().GetLinearVelocity(JPH::BodyID(id.value)));
    }

    void PhysicsWorld::AddImpulse(BodyId id, Float3 impulse)
    {
        m_impl->system->GetBodyInterface().AddImpulse(JPH::BodyID(id.value), ToJph(impulse));
    }

    void PhysicsWorld::AddForce(BodyId id, Float3 force)
    {
        m_impl->system->GetBodyInterface().AddForce(JPH::BodyID(id.value), ToJph(force),
                                                    JPH::EActivation::Activate);
    }

    bool PhysicsWorld::IsActive(BodyId id) const
    {
        return m_impl->system->GetBodyInterface().IsActive(JPH::BodyID(id.value));
    }

    u64 PhysicsWorld::UserData(BodyId id) const
    {
        return m_impl->system->GetBodyInterface().GetUserData(JPH::BodyID(id.value));
    }

    bool PhysicsWorld::RayCast(Float3 from, Float3 direction, f32 maxDistance, RayHit& out) const
    {
        const JPH::RRayCast ray{ ToJph(from), ToJph(direction) * maxDistance };
        JPH::RayCastResult hit;
        if (!m_impl->system->GetNarrowPhaseQuery().CastRay(ray, hit)) { return false; }
        out.body = BodyId{ hit.mBodyID.GetIndexAndSequenceNumber() };
        out.fraction = hit.mFraction;
        out.position = Float3{ from.x + direction.x * maxDistance * hit.mFraction,
                               from.y + direction.y * maxDistance * hit.mFraction,
                               from.z + direction.z * maxDistance * hit.mFraction };
        out.userData = UserData(out.body);
        out.surface = 0;
        JPH::BodyLockRead lock(m_impl->system->GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            out.normal = FromJph(body.GetWorldSpaceSurfaceNormal(
                hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
            JPH::SubShapeID remainder;
            const JPH::Shape* leaf = body.GetShape()->GetLeafShape(hit.mSubShapeID2, remainder);
            if (leaf != nullptr && leaf->GetSubType() == JPH::EShapeSubType::Mesh)
            {
                out.surface = static_cast<const JPH::MeshShape*>(leaf)->GetTriangleUserData(remainder);
            }
        }
        return true;
    }

    void PhysicsWorld::QueryPoint(Float3 point, Array<BodyId>& out) const
    {
        JPH::AllHitCollisionCollector<JPH::CollidePointCollector> collector;
        m_impl->system->GetNarrowPhaseQuery().CollidePoint(ToJph(point), collector);
        for (const JPH::CollidePointResult& result : collector.mHits)
        {
            out.PushBack(BodyId{ result.mBodyID.GetIndexAndSequenceNumber() });
        }
    }

    void PhysicsWorld::DrainContacts(Array<ContactEvent>& out)
    {
        ScopedLock lock(m_impl->contacts.mutex);
        for (ContactEvent& e : m_impl->contacts.events) { out.PushBack(e); }
        m_impl->contacts.events.Clear();
    }
}
