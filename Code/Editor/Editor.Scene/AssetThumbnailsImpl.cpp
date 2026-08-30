// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Scene-domain GPU thumbnail generators (see AssetThumbnails.cppm). Heavy imports live here.

module;
#include "Core/Prelude.h"

module editor.scene;

import foundation.core;
import foundation.scene;
import foundation.scene.resource;
import foundation.resource;
import foundation.content;
import foundation.geometry;
import foundation.materials;
import engine.render;
import engine.particles;
import editor.core;

using namespace foundation::core;

namespace editor
{
    namespace scene = foundation::scene;
    namespace geometry = foundation::geometry;
    namespace materials = foundation::materials;
    namespace resource = foundation::resource;

    namespace
    {
        // Shared persistent-entity plumbing: a display entity (MeshComponent) + a private sun,
        // created on first use and toggled ACTIVE per job so idle generators contribute nothing
        // to another generator's render.
        struct StageEntities
        {
            scene::EntityHandle display{};
            scene::EntityHandle sun{};

            [[nodiscard]] engine::render::MeshComponent* Ensure(scene::Scene& stage,
                                                                StringView name)
            {
                auto* meshes = stage.GetSystem<engine::render::MeshComponentManager>();
                auto* lights = stage.GetSystem<engine::render::LightComponentManager>();
                if (meshes == nullptr || lights == nullptr)
                {
                    return nullptr;
                }
                if (!display.IsAssigned() || !stage.IsValid(display))
                {
                    display = stage.CreateEntity(name);
                    meshes->Add(display);
                    sun = stage.CreateEntity(u8"ThumbSun");
                    Transform t;
                    t.rotation = Quaternion::FromAxisAngle(Float3{0, 1, 0}, 0.35f) *
                                 Quaternion::FromAxisAngle(Float3{1, 0, 0}, -1.05f);
                    stage.SetLocalTransform(sun, t);
                    engine::render::LightComponent& light = lights->Add(sun);
                    light.castsShadows = false; // a lone preview object has nothing to shadow
                }
                stage.SetActive(display, true);
                stage.SetActive(sun, true);
                return meshes->Get(display);
            }

            void Deactivate(scene::Scene& stage)
            {
                if (display.IsAssigned() && stage.IsValid(display))
                {
                    stage.SetActive(display, false);
                }
                if (sun.IsAssigned() && stage.IsValid(sun))
                {
                    stage.SetActive(sun, false);
                }
            }
        };

        // A null product maps to the handle's state: still loading = Pending (retry next
        // frame), Failed / no handle at all = Failed (negative-cache, stop retrying).
        template <typename T>
        [[nodiscard]] ThumbnailStageStep StepForPending(const resource::Proxy<T>& proxy)
        {
            resource::ResourceHandle* handle = proxy.Handle();
            if (handle == nullptr || handle->State() == resource::ResourceState::Failed)
            {
                return ThumbnailStageStep::Failed;
            }
            return ThumbnailStageStep::Pending;
        }

        // Meshes: the cooked StaticMesh/SkinnedMesh product, centered at the origin by its
        // bounds, neutral PBR material, framed by the half-diagonal.
        class MeshThumbnailGenerator final : public ISceneThumbnailGenerator
        {
        public:
            [[nodiscard]] Span<const StringView> AssetTypeNames() const override
            {
                static constexpr StringView kNames[] = {u8"StaticMeshAsset",
                                                        u8"SkinnedMeshAsset"};
                return Span<const StringView>(kNames, 2);
            }

            [[nodiscard]] ThumbnailStageStep Stage(const Guid& id, scene::Scene& stage,
                                                   resource::ResourceManager& resources,
                                                   ThumbnailFraming& outFraming) override
            {
                engine::render::MeshComponent* component =
                    m_entities.Ensure(stage, u8"ThumbMesh");
                if (component == nullptr)
                {
                    return ThumbnailStageStep::Failed;
                }
                m_proxy = resources.Bind<geometry::StaticMesh>(id);
                geometry::StaticMesh* mesh = m_proxy ? m_proxy.Get() : nullptr;
                if (mesh == nullptr)
                {
                    return StepForPending(m_proxy);
                }
                component->mesh.SetId(Guid{});
                component->mesh = mesh; // direct override to the cooked product
                if (!m_material)
                {
                    m_material = materials::CreatePBR(u8"ThumbMeshDefault");
                }
                component->SetMaterial(m_material);

                // Center the bounds at the origin (the stage's ortho camera looks at 0,0,0).
                Transform t;
                t.position = Float3{} - mesh->bounds.Center();
                stage.SetLocalTransform(m_entities.display, t);
                outFraming.radius = Max(Length(mesh->bounds.Extents()), 0.05f);
                return ThumbnailStageStep::Ready;
            }

            void Unstage(scene::Scene& stage) override
            {
                if (auto* meshes = stage.GetSystem<engine::render::MeshComponentManager>())
                {
                    if (engine::render::MeshComponent* component =
                            meshes->Get(m_entities.display))
                    {
                        component->mesh.SetId(Guid{});
                        component->mesh.SetDirect(RefPtr<geometry::StaticMesh>{});
                    }
                }
                m_proxy = {};
                m_entities.Deactivate(stage);
            }

        private:
            StageEntities m_entities;
            resource::Proxy<geometry::StaticMesh> m_proxy;
            RefPtr<materials::Material> m_material;
        };

        // Materials: the cooked material product on a unit sphere.
        class MaterialThumbnailGenerator final : public ISceneThumbnailGenerator
        {
        public:
            [[nodiscard]] Span<const StringView> AssetTypeNames() const override
            {
                static constexpr StringView kNames[] = {u8"MaterialAsset"};
                return Span<const StringView>(kNames, 1);
            }

            [[nodiscard]] ThumbnailStageStep Stage(const Guid& id, scene::Scene& stage,
                                                   resource::ResourceManager& resources,
                                                   ThumbnailFraming& outFraming) override
            {
                engine::render::MeshComponent* component =
                    m_entities.Ensure(stage, u8"ThumbSphere");
                if (component == nullptr)
                {
                    return ThumbnailStageStep::Failed;
                }
                if (!m_sphere)
                {
                    m_sphere = geometry::Primitives::Sphere(1.0f, 48, 24);
                }
                m_proxy = resources.Bind<materials::Material>(id);
                materials::Material* material = m_proxy ? m_proxy.Get() : nullptr;
                if (material == nullptr)
                {
                    return StepForPending(m_proxy);
                }
                component->mesh.SetId(Guid{});
                component->mesh = m_sphere.Get();
                component->SetMaterial(RefPtr<materials::Material>(material));
                stage.SetLocalTransform(m_entities.display, Transform{});
                outFraming.radius = 1.0f;
                return ThumbnailStageStep::Ready;
            }

            void Unstage(scene::Scene& stage) override
            {
                if (auto* meshes = stage.GetSystem<engine::render::MeshComponentManager>())
                {
                    if (engine::render::MeshComponent* component =
                            meshes->Get(m_entities.display))
                    {
                        component->mesh.SetId(Guid{});
                        component->mesh.SetDirect(RefPtr<geometry::StaticMesh>{});
                        component->materials.Clear();
                        component->materialCache.Clear();
                    }
                }
                m_proxy = {};
                m_entities.Deactivate(stage);
            }

        private:
            StageEntities m_entities;
            resource::Proxy<materials::Material> m_proxy;
            RefPtr<geometry::StaticMesh> m_sphere;
        };

        // Source-payload access for the composite generators (prefab spawn, nested-instance
        // resolution, scene load). Resolved per call - the context outlives the service, the
        // project may not.
        [[nodiscard]] foundation::content::Instance* SourceInstance(EditorContext* context,
                                                                    const Guid& id)
        {
            if (context == nullptr || context->Project() == nullptr)
            {
                return nullptr;
            }
            return context->Project()->SourceDb().GetInstance(id);
        }

        [[nodiscard]] scene::PrefabPayloadResolver PayloadResolver(EditorContext* context)
        {
            return scene::PrefabPayloadResolver{
                [context](const Guid& prefabId) -> UniquePtr<IStream>
                {
                    foundation::content::Instance* instance = SourceInstance(context, prefabId);
                    return (instance != nullptr) ? instance->ReadData(u8"scene")
                                                 : UniquePtr<IStream>{};
                }};
        }

        // Loading settles when every mesh ref with an identity has resolved or failed (a
        // broken ref skips - one missing submesh must not kill the composite's thumbnail).
        [[nodiscard]] bool MeshRefsSettled(scene::Scene& stage)
        {
            auto* meshes = stage.GetSystem<engine::render::MeshComponentManager>();
            if (meshes == nullptr)
            {
                return true;
            }
            bool settled = true;
            meshes->ForEach(
                [&](engine::render::MeshComponent& c, scene::EntityHandle)
                {
                    if (c.mesh.id.IsNil() || c.mesh.Get() != nullptr)
                    {
                        return;
                    }
                    resource::ResourceHandle* handle = c.mesh.GetProxy().Handle();
                    if (handle != nullptr && handle->State() != resource::ResourceState::Failed)
                    {
                        settled = false;
                    }
                });
            return settled;
        }

        // Combined world bounds of every resolved mesh (transforms must be current).
        [[nodiscard]] AABB WorldMeshBounds(scene::Scene& stage)
        {
            AABB bounds = AABB::Empty();
            auto* meshes = stage.GetSystem<engine::render::MeshComponentManager>();
            if (meshes == nullptr)
            {
                return bounds;
            }
            meshes->ForEach(
                [&](engine::render::MeshComponent& c, scene::EntityHandle entity)
                {
                    geometry::StaticMesh* mesh = c.mesh.Get();
                    if (mesh == nullptr || !mesh->bounds.IsValid())
                    {
                        return;
                    }
                    const Float4x4 world = stage.GetWorldMatrix(entity);
                    const Float3 mn = mesh->bounds.min;
                    const Float3 mx = mesh->bounds.max;
                    for (u32 i = 0; i < 8; ++i)
                    {
                        const Float3 corner{(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y,
                                            (i & 4) ? mx.z : mn.z};
                        bounds.Expand(TransformPoint(corner, world));
                    }
                });
            return bounds;
        }

        void FrameFromBounds(const AABB& bounds, ThumbnailFraming& outFraming)
        {
            if (bounds.IsValid())
            {
                outFraming.center = bounds.Center();
                outFraming.radius = Max(Length(bounds.Extents()), 0.05f);
            }
        }

        void AddSun(scene::Scene& stage)
        {
            auto* lights = stage.GetSystem<engine::render::LightComponentManager>();
            if (lights == nullptr)
            {
                return;
            }
            const scene::EntityHandle sun = stage.CreateEntity(u8"ThumbSun");
            Transform t;
            t.rotation = Quaternion::FromAxisAngle(Float3{0, 1, 0}, 0.35f) *
                         Quaternion::FromAxisAngle(Float3{1, 0, 0}, -1.05f);
            stage.SetLocalTransform(sun, t);
            engine::render::LightComponent& light = lights->Add(sun);
            light.castsShadows = false;
        }

        // Prefabs: the instance spawned into the job's private scene, framed by the combined
        // mesh bounds, lit by a sun (a template carries no scene lighting of its own).
        class PrefabThumbnailGenerator final : public ISceneThumbnailGenerator
        {
        public:
            explicit PrefabThumbnailGenerator(EditorContext& context) : m_context(&context) {}

            [[nodiscard]] Span<const StringView> AssetTypeNames() const override
            {
                static constexpr StringView kNames[] = {u8"PrefabDocument"};
                return Span<const StringView>(kNames, 1);
            }

            [[nodiscard]] bool NeedsPrivateScene() const override { return true; }

            [[nodiscard]] ThumbnailStageStep Stage(const Guid& id, scene::Scene& stage,
                                                   resource::ResourceManager& resources,
                                                   ThumbnailFraming& outFraming) override
            {
                if (!m_spawned)
                {
                    foundation::content::Instance* instance = SourceInstance(m_context, id);
                    UniquePtr<IStream> payload =
                        (instance != nullptr) ? instance->ReadData(u8"scene") : UniquePtr<IStream>{};
                    if (payload.Get() == nullptr)
                    {
                        return ThumbnailStageStep::Failed;
                    }
                    const scene::PrefabPayloadResolver resolver = PayloadResolver(m_context);
                    const scene::EntityHandle root = scene::SpawnPrefab(
                        stage, *payload, id, scene::EntityHandle::Invalid(), nullptr, &resolver);
                    if (!root.IsAssigned())
                    {
                        return ThumbnailStageStep::Failed;
                    }
                    scene::ResolveSceneResources(stage, resources);
                    AddSun(stage);
                    m_spawned = true;
                }
                if (!MeshRefsSettled(stage))
                {
                    return ThumbnailStageStep::Pending;
                }
                stage.UpdateTransforms();
                FrameFromBounds(WorldMeshBounds(stage), outFraming);
                return ThumbnailStageStep::Ready;
            }

            void Unstage(scene::Scene&) override { m_spawned = false; } // scene is per-job

        private:
            EditorContext* m_context;
            bool m_spawned = false;
        };

        // Scene documents: the whole scene loaded into the job's private scene, viewed through
        // its own primary camera when it has one (the bounds framing is the fallback). The
        // scene owns its lighting - no sun.
        class SceneThumbnailGenerator final : public ISceneThumbnailGenerator
        {
        public:
            explicit SceneThumbnailGenerator(EditorContext& context) : m_context(&context) {}

            [[nodiscard]] Span<const StringView> AssetTypeNames() const override
            {
                static constexpr StringView kNames[] = {u8"SceneDocument"};
                return Span<const StringView>(kNames, 1);
            }

            [[nodiscard]] bool NeedsPrivateScene() const override { return true; }

            [[nodiscard]] ThumbnailStageStep Stage(const Guid& id, scene::Scene& stage,
                                                   resource::ResourceManager& resources,
                                                   ThumbnailFraming& outFraming) override
            {
                if (!m_loaded)
                {
                    foundation::content::Instance* instance = SourceInstance(m_context, id);
                    if (instance == nullptr || !scene::LoadScene(*instance, stage).IsOk())
                    {
                        return ThumbnailStageStep::Failed;
                    }
                    const scene::PrefabPayloadResolver resolver = PayloadResolver(m_context);
                    scene::ResolveScenePrefabs(stage, resolver);
                    scene::ResolveSceneResources(stage, resources);
                    m_loaded = true;
                }
                if (!MeshRefsSettled(stage))
                {
                    return ThumbnailStageStep::Pending;
                }
                stage.UpdateTransforms();
                FrameFromBounds(WorldMeshBounds(stage), outFraming);
                outFraming.preferSceneCamera = true;
                return ThumbnailStageStep::Ready;
            }

            void Unstage(scene::Scene&) override { m_loaded = false; } // scene is per-job

        private:
            EditorContext* m_context;
            bool m_loaded = false;
        };

        // Particle effects: an emitter in the job's private scene, prewarmed by the stage so
        // the one rendered frame shows a developed burst (everything is empty at t=0). The
        // framing radius is fixed - particle extents are simulation-dependent and unknown
        // until after the prewarm the stage runs.
        class ParticleThumbnailGenerator final : public ISceneThumbnailGenerator
        {
        public:
            [[nodiscard]] Span<const StringView> AssetTypeNames() const override
            {
                static constexpr StringView kNames[] = {u8"ParticleEffectAsset"};
                return Span<const StringView>(kNames, 1);
            }

            [[nodiscard]] bool NeedsPrivateScene() const override { return true; }

            [[nodiscard]] ThumbnailStageStep Stage(const Guid& id, scene::Scene& stage,
                                                   resource::ResourceManager& resources,
                                                   ThumbnailFraming& outFraming) override
            {
                auto* manager = stage.GetSystem<engine::particles::ParticleEffectComponentManager>();
                if (manager == nullptr)
                {
                    return ThumbnailStageStep::Failed;
                }
                if (!m_staged)
                {
                    const scene::EntityHandle emitter = stage.CreateEntity(u8"ThumbEmitter");
                    engine::particles::ParticleEffectComponent& component = manager->Add(emitter);
                    component.effectAsset.SetId(id);
                    component.effectAsset.Bind(resources);
                    m_emitter = emitter;
                    m_staged = true;
                }
                engine::particles::ParticleEffectComponent* component = manager->Get(m_emitter);
                if (component == nullptr)
                {
                    return ThumbnailStageStep::Failed;
                }
                if (component->effectAsset.Get() == nullptr)
                {
                    resource::ResourceHandle* handle = component->effectAsset.GetProxy().Handle();
                    if (handle == nullptr ||
                        handle->State() == resource::ResourceState::Failed)
                    {
                        return ThumbnailStageStep::Failed;
                    }
                    return ThumbnailStageStep::Pending;
                }
                outFraming.radius = 2.5f;
                outFraming.prewarmSteps = 45; // 0.75s at the fixed step
                return ThumbnailStageStep::Ready;
            }

            void Unstage(scene::Scene&) override
            {
                m_staged = false;
                m_emitter = {};
            }

        private:
            scene::EntityHandle m_emitter{};
            bool m_staged = false;
        };
    }

    void RegisterSceneThumbnailGenerators(ThumbnailService& service, EditorContext& context)
    {
        service.RegisterSceneGenerator(
            MakeUnique<MeshThumbnailGenerator>(DefaultAllocator()));
        service.RegisterSceneGenerator(
            MakeUnique<MaterialThumbnailGenerator>(DefaultAllocator()));
        service.RegisterSceneGenerator(
            MakeUnique<PrefabThumbnailGenerator>(DefaultAllocator(), context));
        service.RegisterSceneGenerator(
            MakeUnique<SceneThumbnailGenerator>(DefaultAllocator(), context));
        service.RegisterSceneGenerator(
            MakeUnique<ParticleThumbnailGenerator>(DefaultAllocator()));
    }
}
