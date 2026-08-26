// Engine::Terrain - the `:renderdata` partition.
//
// The whole-terrain render item: ONE TerrainRenderData per terrain component per frame (the
// manager's extract emits it); the renderer culls + LODs its chunks PER VIEW at resolve time via
// foundation.terrain's ExtractVisibleChunkDraws over these fields - so the whole terrain draws as
// one snapshot item with per-view chunk selection.
//
// Splat = the top-K model (terrain-splat-topk.md): the index/weight texture pair (from the CPU
// SplatWeights), an explicit BASE albedo, the palette Texture2DArray, and the per-palette-layer
// tileScale buffer. paletteCount == 0 or a missing weight pair -> the PS renders pure base;
// no base either -> the height-ramp fallback.

module;
#include "Core/Prelude.h"

export module engine.terrain:renderdata;

import foundation.core;
import foundation.render;
import foundation.rhi;
import foundation.terrain;

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
        f32 lodBias = 0.0f; // per-component LOD bias (negative = finer, positive = coarser)

        // --- Top-K splat material (set 3) ------------------------------------------------------
        // The SplatWeights texture pair (weights RGBA8Unorm + indices RGBA8Uint, Load-only - a
        // filtered index sample interpolates layer ids into garbage, ruling R1).
        rhi::TextureView* weightView = nullptr;
        rhi::TextureView* indexView = nullptr;
        // The BASE layer: shows wherever painted weights don't sum to 1. Null = dummy (white albedo /
        // flat normal / default ORM), per terrain-layer-pbr.md.
        rhi::TextureView* baseAlbedoView = nullptr;
        rhi::TextureView* baseNormalView = nullptr; // null = flat-normal dummy
        rhi::TextureView* baseOrmView = nullptr;    // null = default-ORM dummy
        rhi::TextureView* baseHeightView = nullptr; // null = mid-height dummy (height-blend)
        f32 baseTileScale = 1.0f;
        // The paint palette: a Texture2DArray (one slice per palette layer, cook-resized to a
        // common size) + the per-layer tileScale buffer (slot 0 = base, slot 1+i = palette i). The
        // normal / ORM arrays are null when no palette layer used that map (a 1x1 dummy binds).
        rhi::TextureView* paletteArrayView = nullptr;
        rhi::TextureView* normalArrayView = nullptr;
        rhi::TextureView* ormArrayView = nullptr;
        rhi::TextureView* heightArrayView = nullptr; // null = no palette layer used a height map
        rhi::Buffer* tileScaleBuffer = nullptr;
        u64 tileScaleGeneration = 0; // part of the set-3 cache key (never raw pointers)
        u32 paletteCount = 0;
        // Height-blend soft-skirt width (terrain-height-blend.md); the renderer packs it into the view
        // UBO only when a base or palette height map is present (else the OFF linear path runs).
        f32 heightBlendContrast = 0.25f;
    };
}
