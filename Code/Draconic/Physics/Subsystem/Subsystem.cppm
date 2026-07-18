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
#include <cmath>

export module draconic.physics.subsystem;

export import :components;

import draconic.core;
import draconic.runtime;
import draconic.scene;
import draconic.scene.subsystem;
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

                ShapeDesc own;
                own.kind = c.shape;
                own.halfExtents = c.halfExtents;
                own.radius = c.radius;
                own.halfHeight = c.halfHeight;
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
                        shape.localPosition = lp;
                        shape.localRotation = lr;
                        desc.shapes.PushBack(shape);
                    });
                }

                Float3 position, scale;
                Quaternion rotation;
                if (!Decompose(scene.GetWorldMatrix(e), position, rotation, scale)) { return; }
                desc.position = position;
                desc.rotation = rotation;

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
    class PhysicsSubsystem final : public draconic::runtime::Subsystem,
                                   public dscene::ISceneAware
    {
    public:
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

        // Defined in SubsystemImpl.cpp: interpolation + debug wireframes (render dep).
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

    private:
        Array<SceneEntry> m_systems;
    };
}
