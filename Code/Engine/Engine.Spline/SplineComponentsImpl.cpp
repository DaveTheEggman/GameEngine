// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// engine.spline implementation: reflection + the composition install (heavy REFLECT bodies
// out of the interface).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module engine.spline;

import foundation.core;
import foundation.scene;
import foundation.spline;
import foundation.script.facades;

using namespace foundation::core;

namespace engine::spline
{
    REFLECT_VALUE(SplineComponent, "rtti::engine::spline")
    {
        builder.Attribute("displayName", String(u8"Spline"))
            .Attribute("category", String(u8"Utility"))
            .DataVersion(1);
        // Points are authored by the viewport spline tool, not the inspector; only the loop
        // flag is a direct property.
    }

    void AddSplineSceneManagers(foundation::scene::Scene& scene)
    {
        scene.AddSystem<SplineComponentManager>();
    }

    void RegisterSplineComponentReflection()
    {
        static const bool once = []()
        {
            RttiRegisterValue_SplineComponent();
            return true;
        }();
        (void)once;
    }

    const SplineComponent* SceneSplines::Component(foundation::script::Entity entity) const
    {
        if (scene == nullptr || entity.scene != scene)
        {
            return nullptr;
        }
        auto* manager = scene->GetSystem<SplineComponentManager>();
        return (manager != nullptr) ? manager->Get(entity.Handle()) : nullptr;
    }

    f32 SceneSplines::length(foundation::script::Entity entity) const
    {
        const SplineComponent* component = Component(entity);
        return (component != nullptr) ? component->curve.Length() : 0.0f;
    }

    i32 SceneSplines::pointCount(foundation::script::Entity entity) const
    {
        const SplineComponent* component = Component(entity);
        return (component != nullptr) ? static_cast<i32>(component->curve.points.Size()) : 0;
    }

    bool SceneSplines::isClosed(foundation::script::Entity entity) const
    {
        const SplineComponent* component = Component(entity);
        return component != nullptr && component->curve.closed;
    }

    namespace
    {
        [[nodiscard]] SplineHit MakeHit(const foundation::spline::SplineCurve& curve, f32 t,
                                        const Float4x4& world)
        {
            SplineHit hit;
            hit.valid = true;
            hit.t = t;
            hit.position = TransformPoint(curve.Evaluate(t), world);
            hit.tangent = Normalized(TransformDirection(curve.Tangent(t), world));
            return hit;
        }
    }

    SplineHit SceneSplines::sampleAt(foundation::script::Entity entity, f32 t) const
    {
        const SplineComponent* component = Component(entity);
        if (component == nullptr)
        {
            return {};
        }
        return MakeHit(component->curve, t, scene->GetWorldMatrix(entity.Handle()));
    }

    SplineHit SceneSplines::sampleAtDistance(foundation::script::Entity entity,
                                             f32 distance) const
    {
        const SplineComponent* component = Component(entity);
        if (component == nullptr)
        {
            return {};
        }
        return MakeHit(component->curve, component->curve.DistanceToT(distance),
                       scene->GetWorldMatrix(entity.Handle()));
    }

    SplineHit SceneSplines::closestPoint(foundation::script::Entity entity, f32 x, f32 y,
                                         f32 z) const
    {
        const SplineComponent* component = Component(entity);
        if (component == nullptr)
        {
            return {};
        }
        const Float4x4 world = scene->GetWorldMatrix(entity.Handle());
        // The query point maps into curve-local space; the sample maps back out.
        const Float3 local = TransformPoint(Float3{x, y, z}, Inverse(world));
        const foundation::spline::SplineSample sample =
            component->curve.ClosestPoint(local);
        return MakeHit(component->curve, sample.t, world);
    }

    REFLECT_VALUE(SplineHit, "rtti::engine::spline")
    {
        builder.Property<&SplineHit::valid>("valid");
        builder.Property<&SplineHit::t>("t");
        builder.Property<&SplineHit::position>("position");
        builder.Property<&SplineHit::tangent>("tangent");
    }

    REFLECT_VALUE(SceneSplines, "rtti::engine::spline")
    {
        builder.Method<&SceneSplines::length>("length", {"entity"});
        builder.Method<&SceneSplines::pointCount>("pointCount", {"entity"});
        builder.Method<&SceneSplines::isClosed>("isClosed", {"entity"});
        builder.Method<&SceneSplines::sampleAt>("sampleAt", {"entity", "t"});
        builder.Method<&SceneSplines::sampleAtDistance>("sampleAtDistance",
                                                        {"entity", "distance"});
        builder.Method<&SceneSplines::closestPoint>("closestPoint", {"entity", "x", "y", "z"});
        builder.Method<&SceneSplines::of>("of", {"scene"});
    }

    void RegisterSplineScriptFacade()
    {
        static const bool once = []()
        {
            RegisterSplineComponentReflection();
            RttiRegisterValue_SplineHit();
            GlobalTypeRegistry().Register(TypeOf<SplineHit>());
            foundation::script::RegisterExtraScriptRootType(&TypeOf<SplineHit>());
            foundation::script::RegisterExtraFacadeName(u8"SplineHit");
            RttiRegisterValue_SceneSplines();
            GlobalTypeRegistry().Register(TypeOf<SceneSplines>());
            foundation::script::RegisterExtraScriptRootType(&TypeOf<SceneSplines>());
            foundation::script::RegisterExtraFacadeName(u8"SceneSplines");
            return true;
        }();
        (void)once;
    }
}
