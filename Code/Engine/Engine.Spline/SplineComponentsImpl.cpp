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
}
