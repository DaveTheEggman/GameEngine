// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// engine.vegetation implementation: the manager's cache + extraction, reflection and the
// composition install (heavy bodies kept out of the interface; see gcc-module-interface-hygiene).

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"
#include "Core/Reflection/Reflect.h"
#include "Profiler/Profiler.h" // PROFILE_SCOPE (compiles to nothing when disabled)

module engine.vegetation;

import foundation.core;
import foundation.profiler;
import foundation.scene;
import foundation.geometry;
import foundation.materials;
import foundation.heightfield;
import foundation.terrain;
import foundation.terrain.resource;
import foundation.vegetation;
import foundation.render;
import engine.render;  // PackEntity, CategoryForMaterial (the instanced-mesh emitter's helpers)
import engine.terrain; // TerrainComponentManager (the terrain the layers grow on)

using namespace foundation::core;

namespace engine::vegetation
{
    namespace
    {
        // The terrain a layer entity grows on: its own TerrainComponent, else the nearest
        // ancestor's. Returns null (and leaves `terrainEntity` unassigned) when there is none.
        engine::terrain::TerrainComponent* FindTerrainFor(scene::Scene& scene,
                                                          scene::EntityHandle layerEntity,
                                                          scene::EntityHandle& terrainEntity)
        {
            auto* terrains = scene.GetSystem<engine::terrain::TerrainComponentManager>();
            if (terrains == nullptr)
            {
                return nullptr;
            }
            scene::EntityHandle e = layerEntity;
            for (u32 depth = 0; depth < 64 && e.IsAssigned(); ++depth)
            {
                if (engine::terrain::TerrainComponent* tc = terrains->Get(e))
                {
                    terrainEntity = e;
                    return tc;
                }
                e = scene.GetParent(e);
            }
            return nullptr;
        }

        // The largest axis scale a matrix applies (a bounds radius scales by at most this).
        f32 MaxAxisScale(const Float4x4& m) noexcept
        {
            const f32 sx = Length(Float3{m.m[0][0], m.m[0][1], m.m[0][2]});
            const f32 sy = Length(Float3{m.m[1][0], m.m[1][1], m.m[1][2]});
            const f32 sz = Length(Float3{m.m[2][0], m.m[2][1], m.m[2][2]});
            return Max(sx, Max(sy, sz));
        }
    }

    // ---- reflection --------------------------------------------------------------------------

    REFLECT_ENUM(VegetationPlacement, "rtti::foundation::vegetation")
    {
        builder.Value("Uniform", VegetationPlacement::Uniform);
        builder.Value("Splat", VegetationPlacement::Splat);
        builder.Value("Mask", VegetationPlacement::Mask);
        builder.Value("Scattered", VegetationPlacement::Scattered);
    }

    REFLECT_VALUE(VegetationLayerComponent, "rtti::engine::vegetation")
    {
        builder.Attribute("displayName", String(u8"Vegetation Layer"))
            .Attribute("category", String(u8"Terrain"))
            .DataVersion(1)
            .Property<&VegetationLayerComponent::mesh>("mesh")
            .PropAttribute("description",
                           String(u8"The instanced mesh: a grass card, a tuft, a rock."))
            .Property<&VegetationLayerComponent::material>("material")
            .PropAttribute("description", String(u8"Optional override; none = the mesh's own."))
            .Property<&VegetationLayerComponent::placement>("placement")
            .PropAttribute("description",
                           String(u8"Where it grows: everywhere (Uniform), where a terrain splat "
                                  u8"layer is painted (Splat), a painted mask (Mask), or authored "
                                  u8"instances (Scattered)."))
            .Property<&VegetationLayerComponent::splatLayer>("splatLayer")
            .PropAttribute("displayName", String(u8"Splat Layer"))
            .PropAttribute("description",
                           String(u8"Splat placement: the terrain palette index to follow."))
            .Property<&VegetationLayerComponent::splatThreshold>("splatThreshold")
            .PropAttribute("displayName", String(u8"Splat Threshold"))
            .PropAttribute("description",
                           String(u8"Splat placement: the painted share (0..1) below which "
                                  u8"nothing grows."))
            .Property<&VegetationLayerComponent::maskPlane>("maskPlane")
            .PropAttribute("displayName", String(u8"Mask Plane"))
            .Property<&VegetationLayerComponent::density>("density")
            .PropAttribute("description", String(u8"Instances per square metre."))
            .Property<&VegetationLayerComponent::scaleRange>("scaleRange")
            .PropAttribute("displayName", String(u8"Scale Range"))
            .Property<&VegetationLayerComponent::maxSlopeDegrees>("maxSlopeDegrees")
            .PropAttribute("displayName", String(u8"Max Slope"))
            .PropAttribute("description",
                           String(u8"Degrees from flat above which nothing grows."))
            .Property<&VegetationLayerComponent::heightRange>("heightRange")
            .PropAttribute("displayName", String(u8"Height Range"))
            .PropAttribute("description", String(u8"Terrain-local Y window the layer grows in."))
            .Property<&VegetationLayerComponent::alignToNormal>("alignToNormal")
            .PropAttribute("displayName", String(u8"Align To Normal"))
            .Property<&VegetationLayerComponent::fadeStart>("fadeStart")
            .PropAttribute("displayName", String(u8"Fade Start"))
            .PropAttribute("description", String(u8"Metres from the camera: full density inside."))
            .Property<&VegetationLayerComponent::fadeEnd>("fadeEnd")
            .PropAttribute("displayName", String(u8"Fade End"))
            .PropAttribute("description", String(u8"Metres from the camera: nothing beyond."))
            .Property<&VegetationLayerComponent::castShadows>("castShadows")
            .PropAttribute("displayName", String(u8"Cast Shadows"))
            .PropAttribute("description",
                           String(u8"Off for grass (the single most expensive thing a grass layer "
                                  u8"can do); on for rocks and props."))
            .Property<&VegetationLayerComponent::maxInstancesPerChunk>("maxInstancesPerChunk")
            .PropAttribute("displayName", String(u8"Max Per Chunk"))
            .PropAttribute("description",
                           String(u8"Instances per 64 x 64 quad terrain chunk; a denser layer "
                                  u8"scales its density down to fit."))
            .Property<&VegetationLayerComponent::visible>("visible");
    }

    void AddVegetationSceneManagers(foundation::scene::Scene& scene)
    {
        scene.AddSystem<VegetationLayerComponentManager>();
    }

    void RegisterVegetationComponentReflection()
    {
        static const bool once = []()
        {
            RttiRegisterEnum_VegetationPlacement();
            RttiRegisterValue_VegetationLayerComponent();
            return true;
        }();
        (void)once;
    }

    // ---- the manager ---------------------------------------------------------------------------

    void VegetationLayerComponentManager::InvalidateRegion(
        const heightfield::HeightfieldRegion& region)
    {
        if (!region.IsEmpty())
        {
            m_pendingRegions.PushBack(region);
        }
    }

    usize VegetationLayerComponentManager::BuiltSetCount() const noexcept
    {
        usize n = 0;
        for (const auto& entry : m_caches)
        {
            for (const ChunkSet& set : entry.value.sets)
            {
                n += (set.built && !set.world.IsEmpty()) ? 1 : 0;
            }
        }
        return n;
    }

    usize VegetationLayerComponentManager::InstanceCount() const noexcept
    {
        usize n = 0;
        for (const auto& entry : m_caches)
        {
            for (const ChunkSet& set : entry.value.sets)
            {
                n += set.built ? set.world.Size() : 0;
            }
        }
        return n;
    }

    VegetationLayerComponentManager::LayerCache& VegetationLayerComponentManager::CacheFor(
        scene::EntityHandle layerEntity)
    {
        const u64 key = engine::render::PackEntity(layerEntity);
        if (LayerCache* found = m_caches.Find(key))
        {
            return *found;
        }
        return m_caches.InsertOrAssign(key, LayerCache{});
    }

    void VegetationLayerComponentManager::ResetCache(LayerCache& cache,
                                                     const heightfield::Heightfield& hf,
                                                     const Guid& layerId)
    {
        cache.layerId = layerId;
        cache.heightfieldUid = hf.uid;
        cache.heightfieldVersion = hf.Version();
        cache.splatUid = 0;
        cache.splatVersion = 0;
        cache.composed = false;
        cache.chunks.Clear();
        tmodel::BuildChunks(hf, cache.chunks);
        cache.chunksPerSide = tmodel::ChunksPerSide(hf.Size());
        cache.sets.Clear();
        cache.sets.Resize(cache.chunks.Size());
        for (usize i = 0; i < cache.chunks.Size(); ++i)
        {
            cache.sets[i].key =
                veg::ChunkSeed(layerId, cache.chunks[i].chunkX, cache.chunks[i].chunkZ);
        }
    }

    void VegetationLayerComponentManager::DirtyAll(LayerCache& cache)
    {
        for (ChunkSet& set : cache.sets)
        {
            set.dirty = true;
        }
    }

    void VegetationLayerComponentManager::Compose(LayerCache& cache, ChunkSet& set)
    {
        set.world.Resize(set.local.Size());
        for (usize i = 0; i < set.local.Size(); ++i)
        {
            set.world[i] = set.local[i] * cache.entityWorld;
        }
        // World bounds: the local AABB's corners through the entity matrix.
        AABB world = AABB::Empty();
        const Float3 lo = set.localBounds.min;
        const Float3 hi = set.localBounds.max;
        for (u32 corner = 0; corner < 8; ++corner)
        {
            const Float3 p{(corner & 1u) ? hi.x : lo.x, (corner & 2u) ? hi.y : lo.y,
                           (corner & 4u) ? hi.z : lo.z};
            world.Expand(TransformPoint(p, cache.entityWorld));
        }
        set.worldCenter = world.Center();
        set.worldRadius = Length(world.Extents());
        ++set.version; // the renderer re-uploads the set's buffer on a version change
    }

    void VegetationLayerComponentManager::BuildSet(LayerCache& cache, u32 chunkIndex,
                                                   const heightfield::Heightfield& hf,
                                                   const tmodel::SplatWeights* splat,
                                                   const veg::VegetationLayer& layer,
                                                   const AABB& meshBounds)
    {
        ChunkSet& set = cache.sets[chunkIndex];
        veg::ScatterResult result;
        veg::ScatterChunk(set.key, cache.chunks[chunkIndex], hf, splat, layer, meshBounds, result);
        if (result.densityClamped && !cache.warnedClamp)
        {
            cache.warnedClamp = true;
            LOG_WARNING(u8"Vegetation",
                        u8"layer over budget: {} instances/m2 wanted, {} used (maxInstancesPerChunk "
                        u8"= {})",
                        layer.density, result.effectiveDensity, layer.maxInstancesPerChunk);
        }
        // Build into a FRESH array and swap: the previous array may be borrowed by a snapshot
        // still being recorded (the instanced-mesh borrow rule).
        set.local = Move(result.transforms);
        set.localBounds = result.localBounds;
        set.built = true;
        set.dirty = false;
        Compose(cache, set);
        ++m_builds;
    }

    void VegetationLayerComponentManager::ExtractRenderData(render::ExtractedScene& snapshot)
    {
        PROFILE_SCOPE("Vegetation.Extract");
        if (m_scene == nullptr)
        {
            return;
        }
        for (auto& entry : m_caches)
        {
            entry.value.seenThisFrame = false;
        }
        const bool hasOrigin = snapshot.HasViewOrigin();
        const Float3 origin = snapshot.ViewOrigin();
        u32 budget = m_buildBudget;

        ForEach(
            [&](VegetationLayerComponent& c, scene::EntityHandle e)
            {
                LayerCache& cache = CacheFor(e);
                cache.seenThisFrame = true; // a hidden layer keeps its sets (unhide = no regrow)
                if (!c.visible || !m_scene->IsEffectivelyActive(e))
                {
                    return;
                }
                foundation::geometry::StaticMesh* mesh = c.mesh.Get();
                if (mesh == nullptr)
                {
                    return;
                }
                scene::EntityHandle terrainEntity{};
                engine::terrain::TerrainComponent* tc = FindTerrainFor(*m_scene, e, terrainEntity);
                if (tc == nullptr)
                {
                    return;
                }
                foundation::terrain::TerrainResource* res = tc->terrain.Get();
                if (res == nullptr)
                {
                    return;
                }
                heightfield::Heightfield* hf = res->heightfield.Get();
                if (hf == nullptr || hf->IsEmpty())
                {
                    return;
                }
                const tmodel::SplatWeights* splat = res->weights.Get();
                const Guid layerId = m_scene->GetEntityId(e);
                const veg::VegetationLayer layer = c.ToLayer();
                const u64 layerHash = veg::LayerScatterHash(layer);

                // Identity changes rebuild the whole cache: another heightfield (or its size), the
                // layer's persistent id, the mesh, or any scatter parameter.
                if (cache.chunks.IsEmpty() || cache.heightfieldUid != hf->uid ||
                    !(cache.layerId == layerId) || cache.meshUid != mesh->uid ||
                    cache.layerHash != layerHash)
                {
                    ResetCache(cache, *hf, layerId);
                    cache.layerHash = layerHash;
                    cache.meshUid = mesh->uid;
                    cache.warnedClamp = false;
                }
                // Content changes (a sculpt, a paint) regrow the touched chunks when the editor
                // said which (InvalidateRegion), else every chunk.
                const u64 splatUid = splat != nullptr ? splat->uid : 0;
                const u64 splatVersion = splat != nullptr ? splat->Version() : 0;
                if (cache.heightfieldVersion != hf->Version() || cache.splatUid != splatUid ||
                    cache.splatVersion != splatVersion)
                {
                    if (m_pendingRegions.IsEmpty())
                    {
                        DirtyAll(cache);
                    }
                    else
                    {
                        Array<u32> touched;
                        for (const heightfield::HeightfieldRegion& region : m_pendingRegions)
                        {
                            veg::ChunksTouchedBy(region, cache.chunksPerSide, touched);
                        }
                        for (u32 index : touched)
                        {
                            if (index < cache.sets.Size())
                            {
                                cache.sets[index].dirty = true;
                            }
                        }
                    }
                    cache.heightfieldVersion = hf->Version();
                    cache.splatUid = splatUid;
                    cache.splatVersion = splatVersion;
                }
                // The terrain entity's world matrix places the terrain-local instances; a move
                // recomposes the built sets (no rescatter).
                const Float4x4 entityWorld = m_scene->GetWorldMatrix(terrainEntity);
                if (!cache.composed || !(cache.entityWorld == entityWorld))
                {
                    cache.entityWorld = entityWorld;
                    cache.composed = true;
                    for (ChunkSet& set : cache.sets)
                    {
                        if (set.built)
                        {
                            Compose(cache, set);
                        }
                    }
                }

                const f32 entityScale = MaxAxisScale(entityWorld);
                foundation::materials::Material* material = c.material.Get();
                for (u32 i = 0; i < cache.sets.Size(); ++i)
                {
                    ChunkSet& set = cache.sets[i];
                    // Distance from the view to the chunk (its built bounds, else the terrain's).
                    f32 distance = 0.0f;
                    if (hasOrigin)
                    {
                        Float3 center = set.worldCenter;
                        f32 radius = set.worldRadius;
                        if (!set.built)
                        {
                            const tmodel::TerrainChunk& chunk = cache.chunks[i];
                            center = TransformPoint(chunk.bounds.Center(), entityWorld);
                            radius = Length(chunk.bounds.Extents()) * entityScale;
                        }
                        distance = Max(0.0f, Length(origin - center) - radius);
                        if (distance >= layer.fadeEnd)
                        {
                            continue; // out of range: absent from the snapshot (renderer evicts)
                        }
                    }
                    if (set.dirty)
                    {
                        if (budget == 0)
                        {
                            continue; // next frame (the build budget spreads a cold start)
                        }
                        BuildSet(cache, i, *hf, splat, layer, mesh->bounds);
                        --budget;
                    }
                    if (set.world.IsEmpty())
                    {
                        continue;
                    }
                    const f32 density = hasOrigin ? veg::DensityAtDistance(distance, layer.fadeStart,
                                                                           layer.fadeEnd)
                                                  : 1.0f;
                    const u32 count = veg::FadePrefix(static_cast<u32>(set.world.Size()), density);
                    if (count == 0)
                    {
                        continue;
                    }

                    render::MultiMeshRenderData* rd = snapshot.Add<render::MultiMeshRenderData>();
                    if (rd == nullptr)
                    {
                        return;
                    }
                    rd->multiMesh = true;
                    rd->key = set.key;
                    rd->transforms = set.world.Data(); // borrowed for the frame (immutable snapshot)
                    rd->instanceCount = count;         // the fade prefix (per frame)
                    rd->version = set.version;         // the scatter (re-upload only on change)
                    rd->mesh = mesh;
                    rd->material = material;
                    rd->worldCenter = set.worldCenter;
                    rd->worldRadius = set.worldRadius;
                    rd->entityId = engine::render::PackEntity(e);
                    rd->category = engine::render::CategoryForMaterial(material);
                    rd->sortBatchKey = render::BatchKey(mesh, material);
                    rd->castShadows = layer.castShadows;
                }
            });

        // Layers that no longer exist drop their caches (the renderer evicts their buffers).
        Array<u64> stale;
        for (const auto& entry : m_caches)
        {
            if (!entry.value.seenThisFrame)
            {
                stale.PushBack(entry.key);
            }
        }
        for (u64 key : stale)
        {
            (void)m_caches.Remove(key);
        }
        m_pendingRegions.Clear();
    }
}
