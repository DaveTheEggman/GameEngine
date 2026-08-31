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
import foundation.script.facades;

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

    /// A sampled spline result for script - WORLD-space position + unit tangent, plus the
    /// curve parameter and the distance along the curve the sample answers for. `valid` is
    /// false when the entity has no spline (fields zeroed) - the RayCastHit convention.
    struct SplineHit
    {
        bool valid = false;
        f32 t = 0.0f;
        Float3 position{};
        Float3 tangent{};
    };

    /// The scene-bound spline facade (SceneSplines.of(scene), the ScenePhysics idiom): all
    /// queries take the entity CARRYING the SplineComponent and answer in WORLD space (the
    /// entity transform places the curve; points are stored entity-local).
    struct SceneSplines
    {
        foundation::scene::Scene* scene = nullptr;

        [[nodiscard]] f32 length(foundation::script::Entity entity) const;
        [[nodiscard]] i32 pointCount(foundation::script::Entity entity) const;
        [[nodiscard]] bool isClosed(foundation::script::Entity entity) const;
        /// Sample at the curve parameter t (0..segment count; wraps on closed loops).
        [[nodiscard]] SplineHit sampleAt(foundation::script::Entity entity, f32 t) const;
        /// Sample at a distance along the curve (even spacing via the arc-length table).
        [[nodiscard]] SplineHit sampleAtDistance(foundation::script::Entity entity,
                                                 f32 distance) const;
        /// The closest point on the curve to a world position.
        [[nodiscard]] SplineHit closestPoint(foundation::script::Entity entity, f32 x, f32 y,
                                             f32 z) const;

        [[nodiscard]] static SceneSplines of(foundation::script::Scene sceneHandle)
        {
            return SceneSplines{sceneHandle.scene};
        }

    private:
        [[nodiscard]] const SplineComponent* Component(foundation::script::Entity entity) const;
    };

    /// Script surface registration (called from RegisterAllScriptFacades). Idempotent.
    void RegisterSplineScriptFacade();
}
