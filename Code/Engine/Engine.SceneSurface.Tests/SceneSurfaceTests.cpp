// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine.SceneSurface.Tests - the scene-surface composition root.
//
// The full scene set is ONE composition: a single per-domain module list whose `install` entries
// ARE the domain Add<Domain>SceneManagers functions. There is one list, and it delegates to the
// domain functions, so a manager added to a domain function cannot be forgotten from a parallel
// headless list. What this test guards: every DOMAIN is present (the module count), and the
// managers that the export tool's private copy of the list has dropped in the past resolve.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import foundation.net.replication;
import engine.scenesurface;
import engine.render;
import engine.animation;
import engine.script;
import engine.ui;
import engine.terrain;
import engine.spline;
import engine.audio;

using namespace foundation::core;
namespace scene = foundation::scene;

TEST_CASE("engine.scenesurface: full composition covers every domain")
{
    // One module per engine domain + net (the single source of truth in SceneSurfaceImpl).
    CHECK(engine::FullSceneComposition().ModuleCount() == 11u); // + terrain + spline

    // Reproducing the aggregate: Instantiate yields the full manager set with no parallel list.
    scene::Scene scratch(u8"surface");
    engine::AddAllSceneManagers(scratch);

    // The historically-dropped ones stay present by name (each was missing from the export
    // tool's private copy of this list at some point).
    CHECK(scratch.HasSystem<engine::animation::PropertyAnimatorComponentManager>());
    CHECK(scratch.HasSystem<engine::render::PostProcessSystem>());
    CHECK(scratch.HasSystem<engine::script::ScriptComponentManager>());
    CHECK(scratch.HasSystem<engine::ui::UICanvasComponentManager>());
    CHECK(scratch.HasSystem<engine::audio::AudioSourceComponentManager>());
    CHECK(scratch.HasSystem<foundation::net::NetworkComponentManager>());
    CHECK(scratch.HasSystem<engine::terrain::TerrainComponentManager>());
    CHECK(scratch.HasSystem<engine::spline::SplineComponentManager>());

    // Serialization routing works: on-disk type ids resolve to their managers.
    CHECK(scratch.FindManagerBySerializationId(u8"net.Network") != nullptr);
    CHECK(scratch.FindManagerBySerializationId(u8"no.such.component") == nullptr);
}

TEST_CASE("engine.scenesurface: reflection registration is callable and idempotent")
{
    engine::RegisterAllSceneComponentReflection();
    engine::RegisterAllSceneComponentReflection(); // second call must be harmless
}