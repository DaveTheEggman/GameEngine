// engine.terrain:renderdata - the per-terrain render-data payload.
//
// ONE TerrainRenderData rides the draw list per visible terrain component (NOT one per chunk): it
// carries borrowed pointers to the chunk model + quadtree (owned by the manager, valid this frame),
// the GPU height texture, and the terrain's world placement + height mapping. The per-view frustum
// cull + LOD selection happen in TerrainRenderer::Resolve (which has the camera) by replaying
// foundation.terrain's ExtractVisibleChunkDraws over these fields - so the whole terrain draws as one
// item on the sort, and worldCenter/worldRadius (the WHOLE-terrain sphere) keep it from being culled
// while any chunk is visible. Trivially destructible (RenderData contract): only pointers + PODs.

module;
#include "Core/Prelude.h"

export module engine.terrain:renderdata;

import foundation.core;
import foundation.rhi;
import foundation.render; // RenderData base + RenderCategories
import foundation.terrain; // TerrainChunk, TerrainQuadtree (borrowed, CPU model)

using namespace foundation::core;

export namespace engine::terrain
{
    namespace render = foundation::render;
    namespace rhi = foundation::rhi;

    /// The largest LOD-threshold table a terrain carries inline (kMaxChunkLod + 1 = 7 levels).
    inline constexpr u32 kMaxLodThresholds = 8;

    struct TerrainRenderData : render::RenderData
    {
        // SNAPSHOT CPU model: the chunk grid + flattened quadtree nodes, COPIED into the
        // ExtractedScene's frame arena at extraction (AddArray). Never pointers into manager
        // storage - the snapshot is read at record time, after arbitrary scene mutations
        // (the PIE-start UAF: a mid-frame cache rebuild freed the borrowed quadtree).
        const foundation::terrain::TerrainChunk* chunks = nullptr;
        const foundation::terrain::TerrainQuadtree::Node* nodes = nullptr;
        u32 chunkCount = 0;
        u32 nodeCount = 0;

        // The R16Uint height texture (from TerrainHeightTextureCache); the VS/PS fetch it via Load.
        rhi::TextureView* heightView = nullptr;

        // Terrain placement + height mapping (local heightfield space -> world).
        Float4x4 chunkToWorld = Float4x4::Identity();
        i32 gridSize = 0;         // heightfield side S (texture is S x S)
        Float2 worldSizeXZ{0.0f, 0.0f};
        f32 minY = 0.0f;          // world Y at sample 0
        f32 maxY = 0.0f;          // world Y at sample 65535

        // Descending coverage thresholds (thresholds[0] = 1.0 by convention; level 0 = fallback).
        f32 thresholds[kMaxLodThresholds] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        u32 thresholdCount = 1;
        f32 lodBias = 0.0f;

        // D2 splat material (borrowed GPU views; null = absent -> the renderer binds a white dummy and
        // layerCount 0 => the PS falls back to the height ramp). Albedos tile in LOCAL XZ by tileScale.
        static constexpr u32 kMaxLayers = 4;
        rhi::TextureView* splatmapView = nullptr;
        rhi::TextureView* albedoViews[kMaxLayers] = {nullptr, nullptr, nullptr, nullptr};
        f32 tileScales[kMaxLayers] = {1.0f, 1.0f, 1.0f, 1.0f};
        u32 layerCount = 0;
    };
}
