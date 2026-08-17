// Engine.SceneSurface.Tests - the scene-surface composition root.
//
// The TRIPWIRE: AddAllSceneManagers must install exactly kSceneSystemCount scene systems. A
// subsystem author who adds a manager to their Add<Domain>SceneManagers (the function their own
// OnSceneCreated delegates to) bumps the constant deliberately; a registration lost from the
// aggregate - the failure that silently dropped PropertyAnimator/audio/script/UI records from
// exported scenes before this root existed - fails here loudly.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import foundation.net.replication;
import engine.scenesurface;
import engine.render;
import engine.animation;
import engine.audio;
import engine.script;
import engine.ui;

using namespace foundation::core;
namespace scene = foundation::scene;

TEST_CASE("engine.scenesurface: AddAllSceneManagers installs the full set (tripwire)")
{
    scene::Scene scratch(u8"surface");
    engine::AddAllSceneManagers(scratch);

    usize systems = 0;
    scratch.ForEachSystem([&](scene::SceneSystem&) { ++systems; });
    // Add a manager to a domain's Add<Domain>SceneManagers => bump engine::kSceneSystemCount.
    CHECK(systems == engine::kSceneSystemCount);

    // The historically-dropped ones stay present by name (each was missing from the export
    // tool's private copy of this list at some point - see Tools.Export history).
    CHECK(scratch.HasSystem<engine::animation::PropertyAnimatorComponentManager>());
    CHECK(scratch.HasSystem<engine::render::PostProcessSystem>());
    CHECK(scratch.HasSystem<engine::script::ScriptComponentManager>());
    CHECK(scratch.HasSystem<engine::ui::UICanvasComponentManager>());
    CHECK(scratch.HasSystem<engine::audio::AudioSourceComponentManager>());
    CHECK(scratch.HasSystem<foundation::net::NetworkComponentManager>());

    // Serialization routing works: on-disk type ids resolve to their managers.
    CHECK(scratch.FindManagerBySerializationId(u8"net.Network") != nullptr);
    CHECK(scratch.FindManagerBySerializationId(u8"no.such.component") == nullptr);
}

TEST_CASE("engine.scenesurface: reflection registration is callable and idempotent")
{
    engine::RegisterAllSceneComponentReflection();
    engine::RegisterAllSceneComponentReflection(); // second call must be harmless
}
