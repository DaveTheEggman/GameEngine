// engine.terrain:components - the TerrainComponent + its manager.
//
// PHASE A (the authoring surface): TerrainComponent references a cooked TerrainResource (heightfield
// + splatmap + layers, all resolved through the manager) and carries a per-instance cast-shadows +
// visible flag. PHASE C (the renderer feed): the manager is ALSO the scene's IRenderDataProvider - it
// builds the chunk model once per heightfield, gets the GPU height texture from the cache, and emits
// ONE TerrainRenderData per visible terrain (the per-view cull + LOD run later, in the renderer's
// Resolve). Physics is the SEPARATE ShapeKind::Heightfield collider over the same heightfield.

module;
#include "Core/Prelude.h"
#include "Profiler/Profiler.h" // PROFILE_SCOPE (compiles to nothing when disabled)

export module engine.terrain:components;

import foundation.core;
import foundation.profiler;
import foundation.rhi;              // Device, TextureView (the GPU height texture)
import foundation.scene;
import foundation.resource;
import foundation.render;           // IRenderDataProvider, ExtractedScene, RenderCategories
import foundation.heightfield;      // Heightfield (the shared grid)
import foundation.terrain;          // chunk model (BuildChunks, TerrainQuadtree, thresholds)
import foundation.terrain.resource; // TerrainResource
import :heighttexture;              // TerrainHeightTextureCache
import :renderdata;                 // TerrainRenderData

using namespace foundation::core;

export namespace engine::terrain
{
    namespace render = foundation::render;
    namespace rhi = foundation::rhi;
    namespace scene = foundation::scene;
    namespace heightfield = foundation::heightfield;
    namespace tmodel = foundation::terrain;

    using foundation::terrain::TerrainResource;

    struct TerrainComponent
    {
        // The cooked terrain product (Ref<TerrainResource>): heightfield + splatmap + layers. Bound
        // through the manager; the heightfield inside is the SHARED source of truth with the collider.
        foundation::resource::Ref<TerrainResource> terrain;
        bool castShadows = true; // this instance casts into the CSM (terrain default-on)
        bool visible = true;
        f32 lodBias = 0.0f; // +ve = coarser sooner (fewer triangles), -ve = hold detail further out
    };

    inline void Serialize(ISerializer& ar, TerrainComponent& c)
    {
        foundation::core::Serialize(ar, "terrain", c.terrain);
        foundation::core::Serialize(ar, "castShadows", c.castShadows);
        foundation::core::Serialize(ar, "visible", c.visible);
        if (ar.Version() >= 2) // DataVersion 2 added lodBias
        {
            foundation::core::Serialize(ar, "lodBias", c.lodBias);
        }
    }

    inline void ResolveResources(foundation::resource::ResourceManager& manager, TerrainComponent& c)
    {
        c.terrain.Bind(manager);
    }

    class TerrainComponentManager final
        : public foundation::scene::SerializableComponentManager<TerrainComponent>,
          public render::IRenderDataProvider
    {
    public:
        TerrainComponentManager()
            : SerializableComponentManager<TerrainComponent>(u8"terrain")
        {
        }

        void OnSceneCreate(scene::Scene& scene) override { m_scene = &scene; }
        [[nodiscard]] bool IsSimulationOnly() const noexcept override { return false; }

        /// Wired by the TerrainSubsystem after it creates + registers the TerrainRenderer: the device
        /// for the height-texture uploads and the renderer's dispatch id stamped on each render item.
        void SetRenderContext(rhi::Device* device, u16 rendererId) noexcept
        {
            m_device = device;
            m_rendererId = rendererId;
        }

        /// render::IRenderDataProvider: one TerrainRenderData per visible terrain (the WHOLE terrain is
        /// one draw-list item; the renderer culls + LODs its chunks per view). Builds the chunk model
        /// on first sight of a heightfield and the GPU height texture via the cache.
        void ExtractRenderData(render::ExtractedScene& snapshot) override
        {
            PROFILE_SCOPE("Terrain.Extract");
            if (m_device == nullptr)
            {
                return; // no GPU device wired (headless scene consumer): nothing to draw
            }
            ForEach(
                [&](TerrainComponent& c, scene::EntityHandle owner)
                {
                    if (!c.visible || (m_scene != nullptr && !m_scene->IsEffectivelyActive(owner)))
                    {
                        return;
                    }
                    TerrainResource* res = c.terrain.Get();
                    if (res == nullptr)
                    {
                        return;
                    }
                    heightfield::Heightfield* hf = res->heightfield.Get();
                    if (hf == nullptr || hf->IsEmpty())
                    {
                        return;
                    }
                    const ChunkCache& cache = GetOrBuildChunks(hf);
                    if (cache.chunks.IsEmpty())
                    {
                        return;
                    }
                    // The GPU height texture (keyed by the heightfield; re-uploads on a version bump).
                    rhi::TextureView* heightView =
                        m_heightTextures.GetOrCreate(*m_device, *hf, hf->Version());
                    if (heightView == nullptr)
                    {
                        return;
                    }

                    TerrainRenderData* rd = snapshot.Add<TerrainRenderData>();
                    if (rd == nullptr)
                    {
                        return;
                    }
                    rd->category = render::RenderCategories::Opaque;
                    rd->rendererId = m_rendererId;
                    rd->chunks = cache.chunks.Data();
                    rd->quadtree = &cache.quadtree;
                    rd->chunkCount = static_cast<u32>(cache.chunks.Size());
                    rd->heightView = heightView;
                    rd->chunkToWorld = (m_scene != nullptr) ? m_scene->GetWorldMatrix(owner)
                                                            : Float4x4::Identity();
                    rd->gridSize = hf->Size();
                    rd->worldSizeXZ = hf->WorldSize();
                    rd->minY = hf->MinY();
                    rd->maxY = hf->MaxY();

                    const u32 n = Min(static_cast<u32>(kDefaultThresholdCount), kMaxLodThresholds);
                    for (u32 i = 0; i < n; ++i)
                    {
                        rd->thresholds[i] = kDefaultThresholds[i];
                    }
                    rd->thresholdCount = n;
                    rd->lodBias = c.lodBias;

                    // Whole-terrain bounding sphere (world) - keeps the framework from culling the
                    // terrain while any chunk is visible.
                    const Float3 localCenter = cache.localBounds.Center();
                    rd->worldCenter = TransformPoint(localCenter, rd->chunkToWorld);
                    rd->worldRadius = Length(cache.localBounds.Extents());
                });
        }

    private:
        struct ChunkCache
        {
            Array<tmodel::TerrainChunk> chunks;
            tmodel::TerrainQuadtree quadtree;
            AABB localBounds = AABB::Empty();
            u64 version = 0;
        };

        // Chunk model per heightfield (shared by every terrain referencing it). Keyed by the resolved
        // pointer; a hot-reload swap (new pointer) OR a sculpt/regen (version bump) rebuilds - the
        // chunk Y bounds move with the heights, so cull + LOD stay correct.
        const ChunkCache& GetOrBuildChunks(const heightfield::Heightfield* hf)
        {
            if (ChunkCache* found = m_chunkCache.Find(hf); found != nullptr &&
                found->version == hf->Version())
            {
                return *found;
            }
            ChunkCache cache;
            cache.version = hf->Version();
            tmodel::BuildChunks(*hf, cache.chunks);
            cache.quadtree.Build(
                Span<const tmodel::TerrainChunk>{cache.chunks.Data(), cache.chunks.Size()},
                tmodel::ChunksPerSide(hf->Size()));
            for (usize i = 0; i < cache.chunks.Size(); ++i)
            {
                cache.localBounds = Merge(cache.localBounds, cache.chunks[i].bounds);
            }
            m_chunkCache.InsertOrAssign(hf, Move(cache));
            return *m_chunkCache.Find(hf);
        }

        // Descending coverage thresholds over the 7 chunk LODs (thresholds[0] = 1.0 = level-0 fallback).
        static constexpr f32 kDefaultThresholds[] = {1.0f,   0.25f,  0.08f, 0.03f,
                                                      0.012f, 0.005f, 0.002f};
        static constexpr usize kDefaultThresholdCount =
            sizeof(kDefaultThresholds) / sizeof(kDefaultThresholds[0]);

        scene::Scene* m_scene = nullptr;
        rhi::Device* m_device = nullptr;
        u16 m_rendererId = 0;
        TerrainHeightTextureCache m_heightTextures;
        HashMap<const heightfield::Heightfield*, ChunkCache> m_chunkCache;
    };

    // Scene-composition hooks (the SceneModule pair, mirroring every other domain).
    void AddTerrainSceneManagers(foundation::scene::Scene& scene);
    void RegisterTerrainComponentReflection();
}
