// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Engine::Spline - the `engine.spline` module.
///
/// The authorable 3D spline as scene DATA: SplineComponent wraps a foundation.spline
/// SplineCurve (cubic-Bezier points + closed flag) with no per-frame work - consumers
/// (path-follow, scatter, road extrusion, particles) evaluate it; the editor's spline tool
/// authors it. Data-only on purpose: no subsystem, no tick - the manager exists so the
/// component serializes, reflects into the component menu, and rides scene composition.

module;
#include "Core/Prelude.h"

export module engine.spline;

import foundation.core;
import foundation.scene;
import foundation.spline;

using namespace foundation::core;

export namespace engine::spline
{
    namespace fspline = foundation::spline;

    struct SplineComponent
    {
        fspline::SplineCurve curve;
    };

    inline void Serialize(ISerializer& ar, SplineComponent& c)
    {
        foundation::core::Serialize(ar, "points", c.curve.points);
        foundation::core::Serialize(ar, "closed", c.curve.closed);
        if (ar.Mode() == SerializeMode::Read)
        {
            // Stored handles are authoritative; only the derived caches rebuild on load.
            c.curve.RebuildArcLength();
        }
    }

    class SplineComponentManager final
        : public foundation::scene::SerializableComponentManager<SplineComponent>
    {
    public:
        SplineComponentManager()
            : foundation::scene::SerializableComponentManager<SplineComponent>(u8"spline")
        {
        }
    };

    /// Scene-composition install (the domain module entry; see Engine.SceneSurface).
    void AddSplineSceneManagers(foundation::scene::Scene& scene);
    /// Component reflection (component menu + inspector + data-version gate). Idempotent.
    void RegisterSplineComponentReflection();
}
