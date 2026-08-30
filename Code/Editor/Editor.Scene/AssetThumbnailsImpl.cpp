// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Scene-domain GPU thumbnail generators (see AssetThumbnails.cppm). Heavy imports live here.

module;
#include "Core/Prelude.h"

module editor.scene;

import foundation.core;
import foundation.scene;
import foundation.resource;
import foundation.geometry;
import foundation.materials;
import engine.render;
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
                                                   f32& outRadius) override
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
                outRadius = Max(Length(mesh->bounds.Extents()), 0.05f);
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
                                                   f32& outRadius) override
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
                outRadius = 1.0f;
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
    }

    void RegisterSceneThumbnailGenerators(ThumbnailService& service)
    {
        service.RegisterSceneGenerator(
            MakeUnique<MeshThumbnailGenerator>(DefaultAllocator()));
        service.RegisterSceneGenerator(
            MakeUnique<MaterialThumbnailGenerator>(DefaultAllocator()));
    }
}
