// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// SplineComponent: serialization round-trips the authored point set (positions, handles,
// modes, closed flag) and rebuilds the derived arc-length cache on load.

#include <doctest/doctest.h>

#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import foundation.spline;
import foundation.script.facades;
import engine.spline;

using namespace foundation::core;
using namespace foundation::spline;
using engine::spline::SplineComponent;

TEST_CASE("spline component: serialization round-trips points and rebuilds caches")
{
    SplineComponent authored;
    authored.curve.points.PushBack(SplinePoint{Float3{0, 0, 0}});
    authored.curve.points.PushBack(
        SplinePoint{Float3{5, 1, 0}, Float3{-1, 0, 0}, Float3{1, 0, 0},
                    SplineHandleMode::Broken});
    authored.curve.points.PushBack(SplinePoint{Float3{10, 0, 4}});
    authored.curve.closed = true;
    authored.curve.UpdateAutoHandles();
    authored.curve.RebuildArcLength();

    MemoryStream buffer;
    {
        BinarySerializer writer(buffer, SerializeMode::Write);
        Serialize(writer, authored);
        REQUIRE(writer.IsOk());
    }
    (void)buffer.Seek(0, SeekOrigin::Begin);
    SplineComponent loaded;
    {
        BinarySerializer reader(buffer, SerializeMode::Read);
        Serialize(reader, loaded);
        REQUIRE(reader.IsOk());
    }

    REQUIRE(loaded.curve.points.Size() == 3u);
    CHECK(loaded.curve.closed);
    CHECK(Length(loaded.curve.points[1].position - Float3{5, 1, 0}) < 0.0001f);
    CHECK(Length(loaded.curve.points[1].inHandle - Float3{-1, 0, 0}) < 0.0001f);
    CHECK(loaded.curve.points[1].mode == SplineHandleMode::Broken);
    // Derived caches rebuilt on read: the loaded curve evaluates identically.
    CHECK(loaded.curve.Length() == doctest::Approx(authored.curve.Length()).epsilon(0.001));
    CHECK(Length(loaded.curve.Evaluate(1.5f) - authored.curve.Evaluate(1.5f)) < 0.0001f);
}

TEST_CASE("spline facade: world-space queries through SceneSplines")
{
    foundation::scene::Scene sceneObj(u8"splines");
    engine::spline::AddSplineSceneManagers(sceneObj);
    auto* manager = sceneObj.GetSystem<engine::spline::SplineComponentManager>();
    REQUIRE(manager != nullptr);

    const foundation::scene::EntityHandle entity = sceneObj.CreateEntity(u8"path");
    engine::spline::SplineComponent& component = manager->Add(entity);
    component.curve.points.PushBack(SplinePoint{Float3{0, 0, 0}});
    component.curve.points.PushBack(SplinePoint{Float3{10, 0, 0}});
    component.curve.UpdateAutoHandles();
    component.curve.RebuildArcLength();

    // Place the entity: the facade must answer in WORLD space.
    Transform t;
    t.position = Float3{0, 5, 0};
    sceneObj.SetLocalTransform(entity, t);
    sceneObj.UpdateTransforms();

    foundation::script::Entity scriptEntity{&sceneObj, entity.index, entity.generation};
    engine::spline::SceneSplines splines{&sceneObj};

    CHECK(splines.pointCount(scriptEntity) == 2);
    CHECK(!splines.isClosed(scriptEntity));
    CHECK(splines.length(scriptEntity) == doctest::Approx(10.0f).epsilon(0.001));

    const engine::spline::SplineHit mid = splines.sampleAtDistance(scriptEntity, 5.0f);
    CHECK(mid.valid);
    CHECK(Length(mid.position - Float3{5, 5, 0}) < 0.05f);
    CHECK(Length(mid.tangent - Float3{1, 0, 0}) < 0.01f);

    const engine::spline::SplineHit nearest = splines.closestPoint(scriptEntity, 3.0f, 9.0f, 0.0f);
    CHECK(nearest.valid);
    CHECK(Length(nearest.position - Float3{3, 5, 0}) < 0.05f);

    // No spline on the entity -> the invalid hit, zeroed.
    const foundation::scene::EntityHandle bare = sceneObj.CreateEntity(u8"bare");
    foundation::script::Entity bareEntity{&sceneObj, bare.index, bare.generation};
    CHECK(!splines.sampleAt(bareEntity, 0.5f).valid);
    CHECK(splines.length(bareEntity) == 0.0f);
}
