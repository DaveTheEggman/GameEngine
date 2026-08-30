// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Foundation::Lod - the `foundation.lod` module.
//
// Level-of-detail selection math, shared by every LOD system in the engine: mesh chains
// (foundation.render's per-view selection) and terrain chunk geo-mipmapping
// (foundation.terrain) both delegate HERE, so there is exactly one coverage
// formula, one threshold walk, and one hysteresis rule. Pure functions over Core math -
// no RHI, no render types (cameras arrive as plain matrices), unit-testable headless.
//
// Placement history: this briefly lived in core's :bounds partition (the terrain layering
// ruling's first cut); the user ruled LOD is a DOMAIN concept core must not know about,
// so it is a dedicated leaf instead - the designated home for LOD-flavored math that
// would otherwise accrete into core.

module;
#include "Core/Prelude.h"

export module foundation.lod;

import foundation.core;

using namespace foundation::core;

export namespace foundation::lod
{
    /// The fraction of the viewport half-height a bounding sphere's radius spans under
    /// `view` + `projection`. projection[1][1] scales both projection kinds (cot(fovY/2)
    /// perspective, 2/height ortho); m[3][3] discriminates them - perspective divides by
    /// the view-space depth (forward = -z, clamped away from zero), ortho is depth-free.
    /// `bias` halves the result per unit (LOD-bias semantics; 0 = none).
    [[nodiscard]] inline f32 ProjectedSphereCoverage(const Float4x4& view,
                                                     const Float4x4& projection,
                                                     Float3 worldCenter, f32 worldRadius,
                                                     f32 bias = 0.0f) noexcept
    {
        const f32 proj11 = projection.m[1][1];
        f32 coverage;
        if (projection.m[3][3] != 0.0f)
        {
            coverage = worldRadius * proj11; // ortho: screen size is depth-free
        }
        else
        {
            const Float3 vc = TransformPoint(worldCenter, view);
            const f32 depth = ((-vc.z) > 0.001f) ? (-vc.z) : 0.001f;
            coverage = worldRadius * proj11 / depth;
        }
        if (bias != 0.0f)
        {
            coverage *= Pow(2.0f, -bias);
        }
        return coverage;
    }

    /// Descending-threshold LOD walk: level l takes over while coverage < thresholds[l]
    /// (thresholds[0] is 1.0 by convention and unused - level 0 is the fallback). A
    /// malformed (non-descending) tail stops the walk; empty/1-entry always answers 0.
    [[nodiscard]] inline u32 SelectLevelByCoverage(Span<const f32> thresholds,
                                                   f32 coverage) noexcept
    {
        u32 selected = 0;
        for (usize l = 1; l < thresholds.Size(); ++l)
        {
            if (coverage < thresholds[l])
            {
                selected = static_cast<u32>(l);
            }
            else
            {
                break;
            }
        }
        return selected;
    }

    /// Hysteresis: keep `last` while it is still selectable inside the +-`band` coverage
    /// window (an item hovering exactly on a threshold never flickers); otherwise the raw
    /// selection wins - a stale level is never pinned.
    [[nodiscard]] inline u32 ApplyCoverageHysteresis(Span<const f32> thresholds, f32 coverage,
                                                     u32 rawSelection, u32 last,
                                                     f32 band = 0.05f) noexcept
    {
        const u32 finest = SelectLevelByCoverage(thresholds, coverage * (1.0f + band));
        const u32 coarsest = SelectLevelByCoverage(thresholds, coverage * (1.0f - band));
        return (last >= finest && last <= coarsest) ? last : rawSelection;
    }
}
