// Draconic::PhysicsSubsystem - the `draconic.physics.subsystem` module.
//
// Scene integration (docs/design/physics.md §3.2): a PhysicsSceneSystem per scene owns its
// PhysicsWorld; bodies build from RigidBodyComponents (+ descendant ColliderComponents
// compounding) at OnSceneStarted and tear down at OnSceneStopped. Per fixed step:
// kinematic bodies <- scene transforms (MoveKinematic, velocity-correct), the coalesced
// world step, contact drain, dynamic poses -> the component pose double-buffer. Every
// RENDER frame the subsystem writes scene transforms as lerp(prev, curr, FixedAlpha()) -
// the interpolation none of the surveyed engines had. Transform ownership: dynamic =
// physics owns pos/rot (scene edits ignored mid-sim); kinematic = scene owns; static =
// immutable while simulating.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"   // DRACONIC_OBJECT (the Physics facade)
#include <cmath>

export module draconic.physics.subsystem;

export import :components;

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.scene.subsystem;
import draconic.script;
import draconic.physics;
// NOTE: no render imports HERE - the debug-draw path lives in SubsystemImpl.cpp (a module
// implementation unit). Keeping heavyweight imports out of the interface matters for
// GCC's module loader (-fno-module-lazy consumers force-load the whole import graph).

using namespace draconic::core;

export namespace draconic::physics
{
    namespace dscene = draconic::scene;

    class PhysicsSceneSystem final : public dscene::SceneSystem
    {
    public:
        [[nodiscard]] bool IsSimulationOnly() const noexcept override { return true; }

        void OnSceneCreate(dscene::Scene& scene) override { m_scene = &scene; }

        // Scene-settings seam (edited in the scene inspector, persisted with the scene).
        [[nodiscard]] const TypeInfo* SettingsType() const noexcept override
        {
            return &TypeOf<PhysicsSceneSettings>();
        }
        [[nodiscard]] void* SettingsInstance() noexcept override { return &m_settings; }
        [[nodiscard]] StringView SettingsId() const noexcept override { return u8"physics"; }
        void SerializeSettings(ISerializer& ar) override
        {
            draconic::core::Serialize(ar, "gravity", m_settings.gravity);
            draconic::core::Serialize(ar, "collisionSteps", m_settings.collisionSteps);
            draconic::core::Serialize(ar, "debugDraw", m_settings.debugDraw);
        }

        [[nodiscard]] PhysicsSceneSettings& Settings() noexcept { return m_settings; }
        [[nodiscard]] PhysicsWorld* World() noexcept { return m_world.Get(); }
        [[nodiscard]] Span<const ContactEvent> Events() const noexcept
        {
            return Span<const ContactEvent>{ m_events.Data(), m_events.Size() };
        }

        // ---- play lifecycle ----

        void OnSceneStarted() override
        {
            // World matrices are Identity until the first UpdateTransforms - building from
            // stale/never-updated matrices spawns EVERY body at the origin (interpenetrating,
            // then depenetration blasts them apart). Guarantee freshness here rather than
            // trusting every caller of Scene::Start to have updated first.
            m_scene->UpdateTransforms();
            PhysicsWorldSettings settings;
            settings.gravity = m_settings.gravity;
            m_world = MakeUnique<PhysicsWorld>(DefaultAllocator(), settings);
            BuildBodies();
        }

        void OnSceneStopped() override
        {
            auto* bodies = m_scene->GetSystem<RigidBodyComponentManager>();
            if (bodies != nullptr)
            {
                bodies->ForEach([](RigidBodyComponent& c, dscene::EntityHandle) {
                    c.body = BodyId{};
                });
            }
            m_events.Clear();
            m_world = nullptr;
        }

        void OnFixedUpdate(f32 fixedDeltaTime) override
        {
            if (m_world.Get() == nullptr) { return; }
            dscene::Scene& scene = *m_scene;
            auto* bodies = scene.GetSystem<RigidBodyComponentManager>();
            if (bodies == nullptr) { return; }

            // Kinematics follow the SCENE (velocity-correct move toward this step's target).
            bodies->ForEach([&](RigidBodyComponent& c, dscene::EntityHandle e) {
                if (!c.body.IsValid() || c.motion != MotionKind::Kinematic) { return; }
                Float3 position;
                Quaternion rotation;
                Float3 scale;
                if (Decompose(scene.GetWorldMatrix(e), position, rotation, scale))
                {
                    m_world->MoveKinematic(c.body, position, rotation, fixedDeltaTime);
                }
            });

            m_world->Step(fixedDeltaTime, m_settings.collisionSteps < 1 ? 1
                                                                        : m_settings.collisionSteps);

            m_events.Clear();
            m_world->DrainContacts(m_events);

            // Dynamic poses into the double-buffer (prev <- curr <- world).
            bodies->ForEach([&](RigidBodyComponent& c, dscene::EntityHandle) {
                if (!c.body.IsValid() || c.motion != MotionKind::Dynamic) { return; }
                c.prevPosition = c.currPosition;
                c.prevRotation = c.currRotation;
                m_world->GetBodyTransform(c.body, c.currPosition, c.currRotation);
            });
        }

        /// Render-frame interpolation (driven by the subsystem with the engine's fixed
        /// alpha): dynamic entities' scene transforms = lerp(prev, curr, alpha). Writes
        /// WORLD poses converted to local against the current parent.
        void ApplyInterpolation(f32 alpha)
        {
            if (m_world.Get() == nullptr || m_scene == nullptr || !m_scene->SimulationEnabled()) { return; }
            dscene::Scene& scene = *m_scene;
            auto* bodies = scene.GetSystem<RigidBodyComponentManager>();
            if (bodies == nullptr) { return; }
            bodies->ForEach([&](RigidBodyComponent& c, dscene::EntityHandle e) {
                if (!c.body.IsValid() || c.motion != MotionKind::Dynamic) { return; }
                const Float3 position{
                    c.prevPosition.x + (c.currPosition.x - c.prevPosition.x) * alpha,
                    c.prevPosition.y + (c.currPosition.y - c.prevPosition.y) * alpha,
                    c.prevPosition.z + (c.currPosition.z - c.prevPosition.z) * alpha };
                const Quaternion rotation = Slerp(c.prevRotation, c.currRotation, alpha);

                // World -> local against the parent (scale preserved from the current local).
                Transform local = scene.GetLocalTransform(e);
                dscene::EntityHandle parent = scene.GetParent(e);
                if (parent.IsAssigned())
                {
                    const Float4x4 world = Transform{ position, rotation, Float3{ 1, 1, 1 } }.ToMatrix();
                    const Float4x4 parentInverse = Inverse(scene.GetWorldMatrix(parent));
                    Float3 lp, ls;
                    Quaternion lr;
                    if (Decompose(world * parentInverse, lp, lr, ls))
                    {
                        local.position = lp;
                        local.rotation = lr;
                    }
                }
                else
                {
                    local.position = position;
                    local.rotation = rotation;
                }
                scene.SetLocalTransform(e, local);
            });
        }

        [[nodiscard]] dscene::Scene* ScenePtr() const noexcept { return m_scene; }

    private:
        void BuildBodies()
        {
            dscene::Scene& scene = *m_scene;
            auto* bodies = scene.GetSystem<RigidBodyComponentManager>();
            auto* colliders = scene.GetSystem<ColliderComponentManager>();
            if (bodies == nullptr) { return; }

            bodies->ForEach([&](RigidBodyComponent& c, dscene::EntityHandle e) {
                BodyDesc desc;
                desc.motion = c.motion;
                desc.layer = c.layer;
                desc.friction = c.friction;
                desc.restitution = c.restitution;
                desc.linearDamping = c.linearDamping;
                desc.angularDamping = c.angularDamping;
                desc.isTrigger = c.isTrigger;

                // Reverse map: the entity guid's low 64 bits (guids are 128-bit; low is
                // unique enough within one scene for lookups via FindEntity by the system).
                const Guid id = scene.GetEntityId(e);
                desc.userData = id.low;

                Float3 position, scale;
                Quaternion rotation;
                if (!Decompose(scene.GetWorldMatrix(e), position, rotation, scale)) { return; }

                ShapeDesc own;
                own.kind = c.shape;
                own.halfExtents = c.halfExtents;
                own.radius = c.radius;
                own.halfHeight = c.halfHeight;
                own.planeHalfExtent = c.planeHalfExtent;
                if (c.shape == ShapeKind::Cooked)
                {
                    CollisionShape* cooked = c.collisionShape.Get();
                    if (cooked == nullptr)
                    {
                        DRACONIC_LOG_WARNING(u8"Physics",
                            u8"'{}': cooked shape has no collision-shape resource - body skipped",
                            scene.GetEntityName(e));
                        return;
                    }
                    own.cooked = cooked->Blob();
                    own.scale = scale;   // cooked geometry is authored unit-scale
                }
                desc.shapes.PushBack(own);

                // Hierarchy compounding: descendant ColliderComponents fold in at their
                // offset relative to THIS entity (captured at start).
                if (colliders != nullptr)
                {
                    const Float4x4 bodyInverse = Inverse(scene.GetWorldMatrix(e));
                    colliders->ForEach([&](ColliderComponent& extra, dscene::EntityHandle child) {
                        if (!IsDescendantOf(scene, child, e)) { return; }
                        Float3 lp, ls;
                        Quaternion lr;
                        if (!Decompose(scene.GetWorldMatrix(child) * bodyInverse, lp, lr, ls))
                        {
                            return;
                        }
                        ShapeDesc shape;
                        shape.kind = extra.shape;
                        shape.halfExtents = extra.halfExtents;
                        shape.radius = extra.radius;
                        shape.halfHeight = extra.halfHeight;
                        shape.planeHalfExtent = extra.planeHalfExtent;
                        if (extra.shape == ShapeKind::Cooked)
                        {
                            CollisionShape* cooked = extra.collisionShape.Get();
                            if (cooked == nullptr) { return; }
                            shape.cooked = cooked->Blob();
                            shape.scale = ls;
                        }
                        shape.localPosition = lp;
                        shape.localRotation = lr;
                        desc.shapes.PushBack(shape);
                    });
                }

                desc.position = position;
                desc.rotation = rotation;

                // A referenced PhysicalMaterial wins over the inline surface fields.
                if (PhysicalMaterial* material = c.material.Get())
                {
                    desc.friction = material->friction;
                    desc.restitution = material->restitution;
                    desc.density = material->density;
                }

                c.body = m_world->CreateBody(desc);
                c.prevPosition = c.currPosition = position;
                c.prevRotation = c.currRotation = rotation;
                if (!c.body.IsValid())
                {
                    DRACONIC_LOG_WARNING(u8"Physics", u8"body creation failed for '{}'",
                                         scene.GetEntityName(e));
                }
            });
        }

        [[nodiscard]] static bool IsDescendantOf(dscene::Scene& scene, dscene::EntityHandle child,
                                                 dscene::EntityHandle ancestor)
        {
            for (dscene::EntityHandle e = child; e.IsAssigned(); e = scene.GetParent(e))
            {
                if (e == ancestor) { return true; }
            }
            return false;
        }

        dscene::Scene* m_scene = nullptr;   // set by OnSceneCreate
        PhysicsSceneSettings m_settings;
        UniquePtr<PhysicsWorld> m_world;
        Array<ContactEvent> m_events;
    };

    // The runtime subsystem: injects the managers + system into every scene (ISceneAware)
    // and drives render-frame interpolation + debug draw with the engine's fixed alpha.
    /// The service key ExposeToScript binds and the scripting facade resolves.
    inline constexpr StringView kPhysicsScriptService = u8"physics.runtime";

    /// Per-context script binding: which scene's world `Physics.*` calls act on, plus the
    /// last ray hit (Wren methods return one number - the hit accessors read this).
    struct PhysicsScriptBinding
    {
        PhysicsSceneSystem* system = nullptr;
        RayHit lastHit;
        bool lastHitValid = false;
    };

    class PhysicsSubsystem final : public draconic::runtime::Subsystem,
                                   public dscene::ISceneAware
    {
    public:
        /// Binds THIS subsystem's script seam into `context` - the Physics facade acts on
        /// the first STARTED scene's world (the player's/Game tab's single scene).
        void ExposeToScript(draconic::script::IScriptContext& context)
        {
            context.SetService(kPhysicsScriptService, &m_scriptBinding);
        }

        // BEFORE the scene subsystem (-500): the interpolation's local-transform writes
        // must land before Scene::Update recomputes world matrices, or rendering (which
        // extracts world matrices) would lag the physics poses by a frame.
        [[nodiscard]] i32 UpdateOrder() const noexcept override { return -600; }

        void OnSceneCreated(dscene::Scene& scene) override
        {
            scene.AddSystem<RigidBodyComponentManager>();
            scene.AddSystem<ColliderComponentManager>();
            PhysicsSceneSystem* system = scene.AddSystem<PhysicsSceneSystem>();
            m_systems.PushBack(SceneEntry{ &scene, system });
        }
        void OnSceneDestroyed(dscene::Scene& scene) override
        {
            for (usize i = 0; i < m_systems.Size(); ++i)
            {
                if (m_systems[i].scene == &scene) { m_systems.RemoveAt(i); return; }
            }
        }

        // Defined in SubsystemImpl.cpp: interpolation + debug wireframes (render dep)
        // + retargeting the script binding at the first live world.
        void Update(f32 deltaTime) override;

    protected:
        void OnInit() override { RegisterPhysicsComponentReflection(); }
        void OnReady() override
        {
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
                {
                    scenes->RegisterSceneAware(this);
                }
            }
        }
        void OnShutdown() override
        {
            if (draconic::runtime::Context* context = GetContext())
            {
                if (auto* scenes = context->GetSubsystem<dscene::SceneSubsystem>())
                {
                    scenes->UnregisterSceneAware(this);
                }
            }
        }

        struct SceneEntry
        {
            dscene::Scene* scene = nullptr;
            PhysicsSceneSystem* system = nullptr;
        };
        [[nodiscard]] Span<const SceneEntry> Systems() const noexcept
        {
            return Span<const SceneEntry>{ m_systems.Data(), m_systems.Size() };
        }

    protected:
        PhysicsScriptBinding m_scriptBinding;

    private:
        Array<SceneEntry> m_systems;
    };

    // The scripting facade: a foreign class named `Physics` whose STATIC methods resolve
    // the CURRENT script context's bound PhysicsScriptBinding (same seam as the Input
    // facade - no process globals; contexts without the service read released/miss).
    class Physics final : public Object
    {
        DRACONIC_OBJECT(Physics, Object)
    public:
        [[nodiscard]] static PhysicsScriptBinding* Resolve()
        {
            draconic::script::IScriptContext* context = draconic::script::CurrentScriptContext();
            return context != nullptr
                ? static_cast<PhysicsScriptBinding*>(context->GetService(kPhysicsScriptService))
                : nullptr;
        }
        [[nodiscard]] static PhysicsWorld* World()
        {
            PhysicsScriptBinding* binding = Resolve();
            return binding != nullptr && binding->system != nullptr ? binding->system->World()
                                                                    : nullptr;
        }

        /// Distance to the nearest hit, or -1 on a miss. Hit details via the hit* accessors.
        [[nodiscard]] static f32 rayCast(f32 fromX, f32 fromY, f32 fromZ,
                                         f32 directionX, f32 directionY, f32 directionZ,
                                         f32 maxDistance)
        {
            PhysicsScriptBinding* binding = Resolve();
            PhysicsWorld* world = World();
            if (binding == nullptr || world == nullptr) { return -1.0f; }
            binding->lastHitValid = world->RayCast(
                Float3{ fromX, fromY, fromZ }, Float3{ directionX, directionY, directionZ },
                maxDistance, binding->lastHit);
            return binding->lastHitValid ? binding->lastHit.fraction * maxDistance : -1.0f;
        }
        [[nodiscard]] static f32 hitX() { auto* b = Resolve(); return b != nullptr && b->lastHitValid ? b->lastHit.position.x : 0.0f; }
        [[nodiscard]] static f32 hitY() { auto* b = Resolve(); return b != nullptr && b->lastHitValid ? b->lastHit.position.y : 0.0f; }
        [[nodiscard]] static f32 hitZ() { auto* b = Resolve(); return b != nullptr && b->lastHitValid ? b->lastHit.position.z : 0.0f; }
        [[nodiscard]] static f32 hitNormalX() { auto* b = Resolve(); return b != nullptr && b->lastHitValid ? b->lastHit.normal.x : 0.0f; }
        [[nodiscard]] static f32 hitNormalY() { auto* b = Resolve(); return b != nullptr && b->lastHitValid ? b->lastHit.normal.y : 0.0f; }
        [[nodiscard]] static f32 hitNormalZ() { auto* b = Resolve(); return b != nullptr && b->lastHitValid ? b->lastHit.normal.z : 0.0f; }
        /// Material slot of the hit face (cooked triangle meshes; 0 otherwise).
        [[nodiscard]] static f32 hitSurface() { auto* b = Resolve(); return b != nullptr && b->lastHitValid ? static_cast<f32>(b->lastHit.surface) : 0.0f; }

        /// Impulse on the body the last successful rayCast hit.
        static void impulseOnHit(f32 x, f32 y, f32 z)
        {
            PhysicsScriptBinding* binding = Resolve();
            PhysicsWorld* world = World();
            if (binding == nullptr || world == nullptr || !binding->lastHitValid) { return; }
            world->AddImpulse(binding->lastHit.body, Float3{ x, y, z });
        }

        static void setGravity(f32 x, f32 y, f32 z)
        {
            if (PhysicsWorld* world = World()) { world->SetGravity(Float3{ x, y, z }); }
        }
        [[nodiscard]] static f32 gravityY()
        {
            PhysicsWorld* world = World();
            return world != nullptr ? world->Gravity().y : 0.0f;
        }
        [[nodiscard]] static f32 bodyCount()
        {
            PhysicsWorld* world = World();
            return world != nullptr ? static_cast<f32>(world->BodyCount()) : 0.0f;
        }
    };

    /// Registers the facade type (RegisterReflectedTypes then sweeps it into managers).
    void RegisterPhysicsScriptApi();
}
