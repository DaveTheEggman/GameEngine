// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// foundation.vegetation:layer - the parameters of one vegetation layer: WHERE it grows (the
// placement source), HOW DENSE, and the per-instance rules (scale, slope, height, alignment) plus
// the distance fade. Plain data, hashed for cache invalidation; the scene component mirrors these
// fields flat (the reflected inspector edits leaf fields, not nested structs).

module;
#include "Core/Prelude.h"

export module foundation.vegetation:layer;

import foundation.core;

using namespace foundation::core;

export namespace foundation::vegetation
{
    // The placement source: what decides whether a candidate point grows.
    enum class VegetationPlacement : u8
    {
        Uniform,        // everywhere on the terrain (slope + height rules still apply)
        Splat,          // where the terrain's painted splat layer `splatLayer` clears `splatThreshold`
        Mask,           // where the painted vegetation mask plane `maskPlane` has density
        Scattered,      // authored instances (P2); no procedural scatter
        SplatTimesMask, // the splat share times the mask density (a painted mask carves a road
                        // through splat-driven grass: erase the mask along it)
    };

    // The base (unpainted) terrain layer as a `splatLayer` value: grows where NO palette layer is
    // painted (the implicit base weight).
    inline constexpr u32 kSplatBaseLayer = 0xFFFFFFFFu;

    struct VegetationLayer
    {
        VegetationPlacement placement = VegetationPlacement::Splat;
        u32 splatLayer = 0;          // Splat: palette index, or kSplatBaseLayer
        f32 splatThreshold = 0.25f;  // Splat: share (0..1) below which nothing grows
        u32 maskPlane = 0;           // Mask / SplatTimesMask: plane index in the component's mask
        f32 density = 2.0f;          // instances per square metre (Uniform / Splat / Mask)
        Float2 scaleRange{0.8f, 1.2f};
        f32 maxSlopeDegrees = 35.0f; // reject where the surface tilts more than this
        Float2 heightRange{-1.0e6f, 1.0e6f}; // terrain-local Y window (the heightfield's Y)
        bool alignToNormal = false;  // tilt each instance onto the sampled surface normal
        f32 fadeStart = 40.0f;       // metres: full density inside
        f32 fadeEnd = 80.0f;         // metres: nothing beyond
        bool castShadows = false;    // grass defaults off (the most expensive thing grass can do)
        u32 maxInstancesPerChunk = 4096; // memory bound per set; density scales down to fit
    };

    // A hash of every parameter that changes the SCATTER (fade + shadows are per-frame draw
    // state, not part of the set): the cache invalidation key for a layer.
    [[nodiscard]] inline u64 LayerScatterHash(const VegetationLayer& layer) noexcept
    {
        u64 h = HashBytes(&layer.placement, sizeof(layer.placement));
        h = HashBytes(&layer.splatLayer, sizeof(layer.splatLayer), h);
        h = HashBytes(&layer.splatThreshold, sizeof(layer.splatThreshold), h);
        h = HashBytes(&layer.maskPlane, sizeof(layer.maskPlane), h);
        h = HashBytes(&layer.density, sizeof(layer.density), h);
        h = HashBytes(&layer.scaleRange, sizeof(layer.scaleRange), h);
        h = HashBytes(&layer.maxSlopeDegrees, sizeof(layer.maxSlopeDegrees), h);
        h = HashBytes(&layer.heightRange, sizeof(layer.heightRange), h);
        h = HashBytes(&layer.alignToNormal, sizeof(layer.alignToNormal), h);
        h = HashBytes(&layer.maxInstancesPerChunk, sizeof(layer.maxInstancesPerChunk), h);
        return h;
    }
}
