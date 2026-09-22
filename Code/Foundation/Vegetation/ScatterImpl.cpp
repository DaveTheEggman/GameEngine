// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// foundation.vegetation:scatter implementation - see Scatter.cppm for the contract.

module;
#include "Core/Prelude.h"

module foundation.vegetation;

import foundation.core;
import foundation.heightfield;
import foundation.terrain;
import foundation.terrain.resource;

using namespace foundation::core;

namespace foundation::vegetation
{
    namespace
    {
        // The splat texel (nearest) under a terrain-local XZ point: the splat raster spans the
        // whole footprint, like the terrain shader's splat uv (0..1 across the grid).
        f32 SplatShare(const terrain::SplatWeights& splat,
                       const heightfield::Heightfield& heightfield, u32 paletteIndex, f32 localX,
                       f32 localZ) noexcept
        {
            if (splat.IsEmpty())
            {
                return 0.0f;
            }
            const Float2 size = heightfield.WorldSize();
            const f32 u = Clamp(localX / Max(size.x, 1e-6f) + 0.5f, 0.0f, 1.0f);
            const f32 v = Clamp(localZ / Max(size.y, 1e-6f) + 0.5f, 0.0f, 1.0f);
            const i32 sx = Clamp(static_cast<i32>(u * static_cast<f32>(splat.Width() - 1) + 0.5f),
                                 0, splat.Width() - 1);
            const i32 sy = Clamp(static_cast<i32>(v * static_cast<f32>(splat.Height() - 1) + 0.5f),
                                 0, splat.Height() - 1);
            const u8 w = (paletteIndex == kSplatBaseLayer) ? splat.BaseWeight(sx, sy)
                                                            : splat.WeightOfLayer(sx, sy, paletteIndex);
            return static_cast<f32>(w) / 255.0f;
        }

        // A right-handed frame whose Y axis is `up` (row-vector convention: rows = axes).
        Float4x4 FrameFromUp(Float3 up, f32 yaw) noexcept
        {
            // Yaw first (about the terrain-local Y), then tilt the whole frame onto the normal.
            const f32 c = Cos(yaw);
            const f32 s = Sin(yaw);
            const Float3 flatX{c, 0.0f, -s};
            const Float3 flatZ{s, 0.0f, c};
            const Float3 n = Normalized(up);
            // Rotate the flat frame's X onto the plane perpendicular to n; Z completes it.
            Float3 x = flatX - n * Dot(flatX, n);
            if (LengthSquared(x) < 1e-8f)
            {
                x = flatZ - n * Dot(flatZ, n);
            }
            x = Normalized(x);
            const Float3 z = Cross(x, n);
            return Float4x4{{{x.x, x.y, x.z, 0.0f},
                             {n.x, n.y, n.z, 0.0f},
                             {z.x, z.y, z.z, 0.0f},
                             {0.0f, 0.0f, 0.0f, 1.0f}}};
        }
    }

    f32 PlacementShareAt(const VegetationLayer& layer, const heightfield::Heightfield& heightfield,
                         const terrain::SplatWeights* splat, f32 localX, f32 localZ) noexcept
    {
        switch (layer.placement)
        {
        case VegetationPlacement::Uniform:
        case VegetationPlacement::Mask: // P1: the mask plane multiplies in here
            return 1.0f;
        case VegetationPlacement::Splat:
            return splat != nullptr ? SplatShare(*splat, heightfield, layer.splatLayer, localX, localZ)
                                    : 0.0f;
        case VegetationPlacement::Scattered:
        default:
            return 0.0f;
        }
    }

    void ScatterChunk(u64 seed, const terrain::TerrainChunk& chunk,
                      const heightfield::Heightfield& heightfield,
                      const terrain::SplatWeights* splat, const VegetationLayer& layer,
                      const AABB& meshLocalBounds, ScatterResult& out)
    {
        out.transforms.Clear();
        out.localBounds = chunk.bounds;
        out.candidateCount = 0;
        out.effectiveDensity = 0.0f;
        out.densityClamped = false;
        if (heightfield.IsEmpty() || layer.placement == VegetationPlacement::Scattered ||
            layer.density <= 0.0f || layer.maxInstancesPerChunk == 0)
        {
            return;
        }

        const f32 minX = chunk.bounds.min.x;
        const f32 minZ = chunk.bounds.min.z;
        const f32 sizeX = chunk.bounds.max.x - minX;
        const f32 sizeZ = chunk.bounds.max.z - minZ;
        const f32 area = sizeX * sizeZ;
        if (area <= 0.0f)
        {
            return;
        }

        // Candidate budget: density x area, capped per chunk (a layer over budget scales its
        // density down - the memory bound per set is the cap, not the density).
        f32 density = layer.density;
        f32 wanted = density * area;
        const f32 cap = static_cast<f32>(layer.maxInstancesPerChunk);
        if (wanted > cap)
        {
            density = cap / area;
            wanted = cap;
            out.densityClamped = true;
        }
        const u32 candidates = static_cast<u32>(wanted + 0.5f);
        out.candidateCount = candidates;
        out.effectiveDensity = density;
        if (candidates == 0)
        {
            return;
        }

        const f32 scaleMin = Min(layer.scaleRange.x, layer.scaleRange.y);
        const f32 scaleMax = Max(layer.scaleRange.x, layer.scaleRange.y);
        const f32 minNormalY = Cos(Clamp(layer.maxSlopeDegrees, 0.0f, 90.0f) * (kPi / 180.0f));
        const f32 heightMin = Min(layer.heightRange.x, layer.heightRange.y);
        const f32 heightMax = Max(layer.heightRange.x, layer.heightRange.y);

        Random rng(seed);
        out.transforms.Reserve(candidates);
        for (u32 i = 0; i < candidates; ++i)
        {
            // Draw every random number a candidate CAN consume up front, so a rejection never
            // shifts the stream of the ones after it (the accepted set stays a stable prefix
            // thinning of the candidate set as parameters move).
            const f32 x = minX + rng.NextFloat() * sizeX;
            const f32 z = minZ + rng.NextFloat() * sizeZ;
            const f32 keep = rng.NextFloat();
            const f32 yaw = rng.NextFloat() * kTwoPi;
            const f32 scale = scaleMin + rng.NextFloat() * (scaleMax - scaleMin);

            const f32 share = PlacementShareAt(layer, heightfield, splat, x, z);
            if (share <= 0.0f || share < layer.splatThreshold || keep >= share)
            {
                continue;
            }
            const Float3 normal = heightfield.GetNormalAt(x, z);
            if (normal.y < minNormalY)
            {
                continue;
            }
            const f32 y = heightfield.GetHeightAt(x, z);
            if (y < heightMin || y > heightMax)
            {
                continue;
            }

            const Float4x4 rotation = layer.alignToNormal ? FrameFromUp(normal, yaw)
                                                          : Float4x4::RotationY(yaw);
            out.transforms.PushBack(Float4x4::Scale(Float3{scale, scale, scale}) * rotation *
                                    Float4x4::Translation(Float3{x, y, z}));
        }

        // Bounds: the chunk's terrain AABB grown by the mesh's extent at the largest scale (a
        // blade's tip, a rock's overhang), so the set's cull sphere covers every instance.
        if (!out.transforms.IsEmpty() && meshLocalBounds.max.x >= meshLocalBounds.min.x)
        {
            const f32 reach = Length(meshLocalBounds.Extents()) + Length(meshLocalBounds.Center());
            const f32 grow = reach * scaleMax;
            out.localBounds.min = out.localBounds.min - Float3{grow, grow, grow};
            out.localBounds.max = out.localBounds.max + Float3{grow, grow, grow};
        }
    }

    void ChunksTouchedBy(const heightfield::HeightfieldRegion& region, i32 chunksPerSide,
                         Array<u32>& outChunkIndices)
    {
        if (region.IsEmpty() || chunksPerSide <= 0)
        {
            return;
        }
        const i32 last = chunksPerSide - 1;
        // A boundary sample (gx = k * 64) belongs to chunk k-1's far edge AND chunk k's near edge.
        const i32 cx0 = Clamp((region.minX - 1) / terrain::kChunkQuads, 0, last);
        const i32 cx1 = Clamp(region.maxX / terrain::kChunkQuads, 0, last);
        const i32 cz0 = Clamp((region.minZ - 1) / terrain::kChunkQuads, 0, last);
        const i32 cz1 = Clamp(region.maxZ / terrain::kChunkQuads, 0, last);
        for (i32 cz = cz0; cz <= cz1; ++cz)
        {
            for (i32 cx = cx0; cx <= cx1; ++cx)
            {
                const u32 index = static_cast<u32>(cz * chunksPerSide + cx);
                bool seen = false;
                for (u32 existing : outChunkIndices)
                {
                    if (existing == index)
                    {
                        seen = true;
                        break;
                    }
                }
                if (!seen)
                {
                    outChunkIndices.PushBack(index);
                }
            }
        }
    }
}
