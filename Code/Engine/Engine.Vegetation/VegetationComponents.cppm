// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// engine.vegetation:components - the TerrainVegetationComponent + its manager.
//
// ONE component on the terrain entity holds the layers (`Array<VegetationLayer>`: a grass layer, a
// rock layer, a flower layer), exactly the spec's data model. The reflected inspector edits each
// layer in place (the generic list editor builds a per-slot expander of leaf rows through a
// ComponentPropertyPath, the pickers included), so a layer is a list slot, not an entity. The
// manager finds the terrain on the component's entity or the nearest ancestor with a
// TerrainComponent.
//
// The manager is the scene's IRenderDataProvider for vegetation: per (layer, chunk) it scatters
// on demand (Foundation::Vegetation, a pure function of the seed), keeps the set's terrain-local
// instances composed into world space, and emits ONE MultiMeshRenderData per set in range with
// the fade prefix as its count. Sets out of range are absent from the snapshot; the renderer
// evicts their GPU buffers after kMultiMeshEvictFrames. Invalidation: the heightfield uid +
// version, the splat uid + version, the entity world matrix and the layer's scatter hash; a
// region notice (InvalidateRegion, the editor brushes) regrows only the touched chunks.

module;
#include "Core/Prelude.h"

export module engine.vegetation:components;

import foundation.core;
import foundation.scene;
import foundation.resource;
import foundation.geometry;         // StaticMesh (bounds)
import foundation.materials;        // Material
import foundation.heightfield;      // Heightfield + HeightfieldRegion
import foundation.terrain;          // TerrainChunk + BuildChunks
import foundation.terrain.resource; // SplatWeights
import foundation.vegetation;       // VegetationLayer (scatter params) + ScatterChunk + fade
import foundation.render;           // IRenderDataProvider, ExtractedScene, MultiMeshRenderData

using namespace foundation::core;

export namespace engine::vegetation
{
    namespace render = foundation::render;
    namespace scene = foundation::scene;
    namespace heightfield = foundation::heightfield;
    namespace tmodel = foundation::terrain;
    namespace veg = foundation::vegetation;

    using foundation::vegetation::VegetationPlacement;
    using foundation::vegetation::kSplatBaseLayer;

    // One authored layer. The scatter parameters mirror foundation::vegetation::VegetationLayer
    // FLAT (the inspector edits leaf fields); ToScatterLayer() is the bridge to the pure scatter.
    struct VegetationLayer
    {
        String name; // the inspector's slot label ("Grass")
        foundation::resource::Ref<foundation::geometry::StaticMesh> mesh;   // a card, a tuft, a rock
        foundation::resource::Ref<foundation::materials::Material> material; // nil = the mesh's own
        VegetationPlacement placement = VegetationPlacement::Splat;
        u32 splatLayer = 0;         // Splat: palette index; kSplatBaseLayer = the unpainted base
        f32 splatThreshold = 0.25f; // Splat: share below which nothing grows
        u32 maskPlane = 0;          // Mask (P1)
        f32 density = 2.0f;         // instances per square metre
        Float2 scaleRange{0.8f, 1.2f};
        f32 maxSlopeDegrees = 35.0f;
        Float2 heightRange{-1.0e6f, 1.0e6f}; // terrain-local Y window
        bool alignToNormal = false;
        f32 fadeStart = 40.0f; // metres: full density inside
        f32 fadeEnd = 80.0f;   // metres: nothing beyond
        bool castShadows = false;
        u32 maxInstancesPerChunk = 4096;
        bool visible = true;

        [[nodiscard]] veg::VegetationLayer ToScatterLayer() const noexcept
        {
            veg::VegetationLayer layer;
            layer.placement = placement;
            layer.splatLayer = splatLayer;
            layer.splatThreshold = splatThreshold;
            layer.maskPlane = maskPlane;
            layer.density = density;
            layer.scaleRange = scaleRange;
            layer.maxSlopeDegrees = maxSlopeDegrees;
            layer.heightRange = heightRange;
            layer.alignToNormal = alignToNormal;
            layer.fadeStart = fadeStart;
            layer.fadeEnd = fadeEnd;
            layer.castShadows = castShadows;
            layer.maxInstancesPerChunk = maxInstancesPerChunk;
            return layer;
        }
    };

    inline void Serialize(ISerializer& ar, VegetationLayer& l)
    {
        foundation::core::Serialize(ar, "name", l.name);
        foundation::core::Serialize(ar, "mesh", l.mesh);
        foundation::core::Serialize(ar, "material", l.material);
        foundation::core::Serialize(ar, "placement", l.placement);
        foundation::core::Serialize(ar, "splatLayer", l.splatLayer);
        foundation::core::Serialize(ar, "splatThreshold", l.splatThreshold);
        foundation::core::Serialize(ar, "maskPlane", l.maskPlane);
        foundation::core::Serialize(ar, "density", l.density);
        foundation::core::Serialize(ar, "scaleRange", l.scaleRange);
        foundation::core::Serialize(ar, "maxSlopeDegrees", l.maxSlopeDegrees);
        foundation::core::Serialize(ar, "heightRange", l.heightRange);
        foundation::core::Serialize(ar, "alignToNormal", l.alignToNormal);
        foundation::core::Serialize(ar, "fadeStart", l.fadeStart);
        foundation::core::Serialize(ar, "fadeEnd", l.fadeEnd);
        foundation::core::Serialize(ar, "castShadows", l.castShadows);
        foundation::core::Serialize(ar, "maxInstancesPerChunk", l.maxInstancesPerChunk);
        foundation::core::Serialize(ar, "visible", l.visible);
    }

    // The vegetation over one terrain: its layers, on the TerrainComponent's entity.
    struct TerrainVegetationComponent
    {
        Array<VegetationLayer> layers;
        bool visible = true;
    };

    inline void Serialize(ISerializer& ar, TerrainVegetationComponent& c)
    {
        foundation::core::Serialize(ar, "layers", c.layers);
        foundation::core::Serialize(ar, "visible", c.visible);
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager,
                                 TerrainVegetationComponent& c)
    {
        for (VegetationLayer& layer : c.layers)
        {
            layer.mesh.Bind(manager);
            layer.material.Bind(manager);
        }
    }

    class TerrainVegetationComponentManager final
        : public foundation::scene::SerializableComponentManager<TerrainVegetationComponent>,
          public render::IRenderDataProvider
    {
    public:
        // Chunks scattered per extraction (a cold start spreads over frames).
        static constexpr u32 kDefaultBuildBudget = 4;

        TerrainVegetationComponentManager()
            : SerializableComponentManager<TerrainVegetationComponent>(u8"terrainVegetation")
        {
        }

        void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }
        [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

        void SetBuildBudget(u32 chunksPerFrame) noexcept { m_buildBudget = chunksPerFrame; }
        [[nodiscard]] u32 BuildBudget() const noexcept { return m_buildBudget; }

        /// A sculpt or paint over `region` (sample-grid coordinates of the heightfield the layers
        /// grow on) - only the chunks it touches regrow on the next extraction. Without a notice,
        /// a heightfield or splat version bump regrows every chunk (the conservative fallback).
        void InvalidateRegion(const heightfield::HeightfieldRegion& region);

        /// render::IRenderDataProvider: one MultiMeshRenderData per (layer, chunk) in range of
        /// the snapshot's view origin (every chunk when the snapshot has none - headless).
        void ExtractRenderData(render::ExtractedScene& snapshot) override;

        // ---- introspection (tests + the HUD) ----
        [[nodiscard]] usize BuiltSetCount() const noexcept; // sets holding instances
        [[nodiscard]] usize InstanceCount() const noexcept; // instances across built sets
        [[nodiscard]] u64 BuildCount() const noexcept { return m_builds; } // scatters run, ever

    private:
        struct ChunkSet
        {
            u64 key = 0;           // ChunkSeed - the renderer's persistent-buffer key
            u32 version = 0;       // bumps per rebuild (the renderer re-uploads on change)
            bool built = false;
            bool dirty = true;     // needs a (re)scatter before it can draw
            Array<Float4x4> world; // composed instances (terrain-local x entity world)
            Array<Float4x4> local; // the scatter (kept: a moved entity recomposes, no rescatter)
            AABB localBounds = AABB::Empty();
            Float3 worldCenter = Float3{0.0f, 0.0f, 0.0f};
            f32 worldRadius = 0.0f;
        };

        // The cached sets of one (component entity, layer index).
        struct LayerCache
        {
            Guid ownerId;
            u32 layerIndex = 0;
            u64 heightfieldUid = 0;
            u64 heightfieldVersion = 0;
            u64 splatUid = 0;
            u64 splatVersion = 0;
            u64 layerHash = 0;
            u64 meshUid = 0;
            Float4x4 entityWorld = Float4x4::Identity();
            bool composed = false;
            i32 chunksPerSide = 0;
            Array<tmodel::TerrainChunk> chunks;
            Array<ChunkSet> sets; // one per chunk, row-major
            bool seenThisFrame = false;
            bool warnedClamp = false; // the over-budget warning fires once per layer
        };

        [[nodiscard]] static u64 CacheKey(scene::EntityHandle owner, u32 layerIndex) noexcept;
        [[nodiscard]] LayerCache& CacheFor(scene::EntityHandle owner, u32 layerIndex);
        void ResetCache(LayerCache& cache, const heightfield::Heightfield& hf, const Guid& ownerId,
                        u32 layerIndex);
        void DirtyAll(LayerCache& cache);
        void BuildSet(LayerCache& cache, u32 chunkIndex, const heightfield::Heightfield& hf,
                      const tmodel::SplatWeights* splat, const veg::VegetationLayer& layer,
                      const AABB& meshBounds);
        void Compose(LayerCache& cache, ChunkSet& set);
        void ExtractLayer(render::ExtractedScene& snapshot, scene::EntityHandle owner,
                          const Guid& ownerId, u32 layerIndex, const VegetationLayer& authored,
                          const heightfield::Heightfield& hf, const tmodel::SplatWeights* splat,
                          const Float4x4& entityWorld, u32& budget);

        scene::Scene* m_scene = nullptr;
        u32 m_buildBudget = kDefaultBuildBudget;
        u64 m_builds = 0;
        HashMap<u64, LayerCache> m_caches; // key = CacheKey(entity, layer index)
        Array<heightfield::HeightfieldRegion> m_pendingRegions;
    };

    // Scene-composition hooks (the SceneModule pair, mirroring every other domain).
    void AddVegetationSceneManagers(foundation::scene::Scene& scene);
    void RegisterVegetationComponentReflection();
}
