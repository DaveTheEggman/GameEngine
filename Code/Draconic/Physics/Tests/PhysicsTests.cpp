// draconic.physics core tests: headless Jolt world - "determinism-enough" simulation
// (spawn/step/assert poses), the layer matrix, compound building, queries, kinematic
// motion, and contact/trigger buffering.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <cmath>

import draconic.core;
import draconic.physics;

using namespace draconic::core;
using namespace draconic::physics;

namespace
{
    [[nodiscard]] BodyDesc FloorDesc()
    {
        BodyDesc floor;
        floor.motion = MotionKind::Static;
        floor.layer = PhysicsLayer::Static;
        ShapeDesc slab;
        slab.kind = ShapeKind::Box;
        slab.halfExtents = Float3{ 50.0f, 0.5f, 50.0f };
        floor.shapes.PushBack(slab);
        floor.position = Float3{ 0.0f, -0.5f, 0.0f };
        return floor;
    }

    [[nodiscard]] BodyDesc BoxAt(f32 y, MotionKind motion = MotionKind::Dynamic)
    {
        BodyDesc box;
        box.motion = motion;
        box.layer = motion == MotionKind::Static ? PhysicsLayer::Static : PhysicsLayer::Dynamic;
        ShapeDesc cube;
        cube.halfExtents = Float3{ 0.5f, 0.5f, 0.5f };
        box.shapes.PushBack(cube);
        box.position = Float3{ 0.0f, y, 0.0f };
        return box;
    }
}

TEST_CASE("physics: a dynamic box falls under gravity and comes to rest on the floor")
{
    PhysicsWorld world;
    (void)world.CreateBody(FloorDesc());
    BodyDesc drop = BoxAt(5.0f);
    drop.userData = 42;
    const BodyId box = world.CreateBody(drop);
    REQUIRE(box.IsValid());
    CHECK(world.UserData(box) == 42u);

    for (int i = 0; i < 240; ++i) { world.Step(1.0f / 60.0f); }   // 4 seconds

    Float3 position;
    Quaternion rotation;
    world.GetBodyTransform(box, position, rotation);
    CHECK(position.y == doctest::Approx(0.5f).epsilon(0.05));   // resting: half extent above floor
    CHECK(std::fabs(position.x) < 0.01f);
    CHECK(world.LinearVelocity(box).y == doctest::Approx(0.0f).epsilon(0.05));
}

TEST_CASE("physics: static-static never pairs; dynamic collides with static")
{
    PhysicsWorld world;
    (void)world.CreateBody(FloorDesc());
    // A static box INSIDE the floor: no contact events from the overlapping statics.
    BodyDesc buried = BoxAt(0.0f, MotionKind::Static);
    (void)world.CreateBody(buried);
    world.Step(1.0f / 60.0f);
    Array<ContactEvent> events;
    world.DrainContacts(events);
    CHECK(events.IsEmpty());

    // A dynamic box dropped from just above: contact Begin against the floor arrives.
    const BodyId box = world.CreateBody(BoxAt(1.2f));
    for (int i = 0; i < 60; ++i) { world.Step(1.0f / 60.0f); }
    events.Clear();
    world.DrainContacts(events);
    bool sawBegin = false;
    for (const ContactEvent& e : events) { if (e.kind == ContactKind::Begin) { sawBegin = true; } }
    CHECK(sawBegin);
    (void)box;
}

TEST_CASE("physics: triggers sense without colliding")
{
    PhysicsWorld world;
    (void)world.CreateBody(FloorDesc());

    BodyDesc sensor;
    sensor.motion = MotionKind::Static;   // static sensors don't pair with statics...
    sensor.isTrigger = true;
    ShapeDesc volume;
    volume.halfExtents = Float3{ 1.0f, 1.0f, 1.0f };
    sensor.shapes.PushBack(volume);
    sensor.position = Float3{ 0.0f, 2.0f, 0.0f };
    sensor.userData = 7;
    // ...so make it kinematic (a trigger VOLUME that can also move).
    sensor.motion = MotionKind::Kinematic;
    const BodyId trigger = world.CreateBody(sensor);
    REQUIRE(trigger.IsValid());

    BodyDesc drop = BoxAt(5.0f);
    drop.userData = 42;
    const BodyId box = world.CreateBody(drop);

    bool entered = false;
    Float3 position;
    Quaternion rotation;
    for (int i = 0; i < 240; ++i)
    {
        world.Step(1.0f / 60.0f);
        Array<ContactEvent> events;
        world.DrainContacts(events);
        for (const ContactEvent& e : events)
        {
            if (e.kind == ContactKind::TriggerEnter
                && (e.userA == 7u || e.userB == 7u)
                && (e.userA == 42u || e.userB == 42u)) { entered = true; }
        }
    }
    CHECK(entered);
    // The sensor produced no collision RESPONSE: the box fell straight through to the floor.
    world.GetBodyTransform(box, position, rotation);
    CHECK(position.y == doctest::Approx(0.5f).epsilon(0.05));
}

TEST_CASE("physics: compound bodies build and simulate")
{
    PhysicsWorld world;
    (void)world.CreateBody(FloorDesc());

    // A dumbbell: two spheres offset on x - lands and rests HIGHER than a bare sphere
    // would if it were a point, proving the compound extent matters.
    BodyDesc dumbbell;
    ShapeDesc left;
    left.kind = ShapeKind::Sphere;
    left.radius = 0.5f;
    left.localPosition = Float3{ -1.0f, 0.0f, 0.0f };
    ShapeDesc right = left;
    right.localPosition = Float3{ 1.0f, 0.0f, 0.0f };
    dumbbell.shapes.PushBack(left);
    dumbbell.shapes.PushBack(right);
    dumbbell.position = Float3{ 0.0f, 4.0f, 0.0f };
    const BodyId body = world.CreateBody(dumbbell);
    REQUIRE(body.IsValid());

    for (int i = 0; i < 240; ++i) { world.Step(1.0f / 60.0f); }
    Float3 position;
    Quaternion rotation;
    world.GetBodyTransform(body, position, rotation);
    CHECK(position.y == doctest::Approx(0.5f).epsilon(0.1));   // resting on the sphere radius
}

TEST_CASE("physics: ray casts hit the nearest body with user data + normal")
{
    PhysicsWorld world;
    (void)world.CreateBody(FloorDesc());
    BodyDesc target = BoxAt(0.5f, MotionKind::Static);
    target.userData = 99;
    (void)world.CreateBody(target);

    RayHit hit;
    REQUIRE(world.RayCast(Float3{ 0.0f, 10.0f, 0.0f }, Float3{ 0.0f, -1.0f, 0.0f }, 100.0f, hit));
    CHECK(hit.userData == 99u);                                  // the box, not the floor
    CHECK(hit.position.y == doctest::Approx(1.0f).epsilon(0.02)); // its top face
    CHECK(hit.normal.y == doctest::Approx(1.0f).epsilon(0.02));   // pointing up

    // A miss stays a miss.
    RayHit miss;
    CHECK_FALSE(world.RayCast(Float3{ 500.0f, 10.0f, 0.0f }, Float3{ 0.0f, 1.0f, 0.0f }, 10.0f, miss));
}

TEST_CASE("physics: kinematic bodies follow MoveKinematic with velocity")
{
    PhysicsWorld world;
    BodyDesc platform = BoxAt(0.0f, MotionKind::Kinematic);
    platform.layer = PhysicsLayer::Kinematic;
    const BodyId body = world.CreateBody(platform);

    // March it +x at 1 unit per step-second.
    Float3 position{ 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 60; ++i)
    {
        position.x += 1.0f / 60.0f;
        world.MoveKinematic(body, position, Quaternion::Identity, 1.0f / 60.0f);
        world.Step(1.0f / 60.0f);
    }
    Float3 result;
    Quaternion rotation;
    world.GetBodyTransform(body, result, rotation);
    CHECK(result.x == doctest::Approx(1.0f).epsilon(0.02));
    CHECK(world.LinearVelocity(body).x == doctest::Approx(1.0f).epsilon(0.1));
}

TEST_CASE("physics: point query finds containing bodies")
{
    PhysicsWorld world;
    BodyDesc box = BoxAt(0.0f, MotionKind::Static);
    const BodyId body = world.CreateBody(box);
    Array<BodyId> hits;
    world.QueryPoint(Float3{ 0.0f, 0.0f, 0.0f }, hits);
    REQUIRE(hits.Size() == 1);
    CHECK(hits[0] == body);
    hits.Clear();
    world.QueryPoint(Float3{ 10.0f, 0.0f, 0.0f }, hits);
    CHECK(hits.IsEmpty());
}
