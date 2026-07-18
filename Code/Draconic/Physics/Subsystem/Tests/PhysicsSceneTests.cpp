// draconic.physics.subsystem tests: the scene integration headless - component-driven
// body building (incl. hierarchy compounding), the fixed-step sync, render-frame
// interpolation between fixed poses, kinematic scene-follow, and play-cycle teardown.

#include <doctest/doctest.h>

#include "Core/Prelude.h"
#include <cmath>

import draconic.core;
import draconic.scene;
import draconic.physics;
import draconic.physics.subsystem;

using namespace draconic::core;
using namespace draconic::physics;
namespace dscene = draconic::scene;

namespace
{
    struct PlayScene
    {
        dscene::Scene scene{ u8"physics-test" };
        PhysicsSceneSystem* physics = nullptr;

        PlayScene()
        {
            RegisterPhysicsComponentReflection();
            scene.AddSystem<RigidBodyComponentManager>();
            scene.AddSystem<ColliderComponentManager>();
            physics = scene.AddSystem<PhysicsSceneSystem>();
        }

        dscene::EntityHandle AddFloor()
        {
            dscene::EntityHandle e = scene.CreateEntity(u8"floor");
            scene.SetLocalPosition(e, Float3{ 0.0f, -0.5f, 0.0f });
            RigidBodyComponent& body = scene.GetSystem<RigidBodyComponentManager>()->Add(e);
            body.motion = MotionKind::Static;
            body.layer = PhysicsLayer::Static;
            body.halfExtents = Float3{ 50.0f, 0.5f, 50.0f };
            return e;
        }

        dscene::EntityHandle AddBox(f32 y, MotionKind motion = MotionKind::Dynamic)
        {
            dscene::EntityHandle e = scene.CreateEntity(u8"box");
            scene.SetLocalPosition(e, Float3{ 0.0f, y, 0.0f });
            RigidBodyComponent& body = scene.GetSystem<RigidBodyComponentManager>()->Add(e);
            body.motion = motion;
            body.layer = motion == MotionKind::Static ? PhysicsLayer::Static
                       : motion == MotionKind::Kinematic ? PhysicsLayer::Kinematic
                                                         : PhysicsLayer::Dynamic;
            return e;
        }

        void Start()
        {
            scene.UpdateTransforms();   // world matrices current before body building
            scene.Start();
            scene.SetSimulationEnabled(true);
        }

        void Step(int steps = 1)
        {
            for (int i = 0; i < steps; ++i) { scene.FixedUpdate(1.0f / 60.0f); }
        }
    };
}

TEST_CASE("physics.scene: components build bodies at Start; dynamics fall and land")
{
    PlayScene play;
    (void)play.AddFloor();
    const dscene::EntityHandle box = play.AddBox(5.0f);
    play.Start();
    REQUIRE(play.physics->World() != nullptr);
    CHECK(play.physics->World()->BodyCount() == 2u);

    play.Step(240);
    play.physics->ApplyInterpolation(1.0f);   // write final poses to the scene
    play.scene.UpdateTransforms();
    const Float3 rest = play.scene.GetWorldPosition(box);
    CHECK(rest.y == doctest::Approx(0.5f).epsilon(0.05));

    // Stop tears the world down and clears handles.
    play.scene.Stop();
    CHECK(play.physics->World() == nullptr);
    CHECK_FALSE(play.scene.GetSystem<RigidBodyComponentManager>()->Get(box)->body.IsValid());
}

TEST_CASE("physics.scene: interpolation blends between the last two fixed poses")
{
    PlayScene play;
    const dscene::EntityHandle box = play.AddBox(10.0f);   // free fall, no floor
    play.Start();
    play.Step(30);   // let it pick up speed

    RigidBodyComponent* body = play.scene.GetSystem<RigidBodyComponentManager>()->Get(box);
    REQUIRE(body != nullptr);
    const f32 prevY = body->prevPosition.y;
    const f32 currY = body->currPosition.y;
    REQUIRE(prevY > currY);   // falling

    // Production order: interpolation (physics subsystem, -600) THEN Scene::Update's
    // UpdateTransforms - mirrored explicitly here.
    play.physics->ApplyInterpolation(0.0f);
    play.scene.UpdateTransforms();
    CHECK(play.scene.GetWorldPosition(box).y == doctest::Approx(prevY).epsilon(0.001));
    play.physics->ApplyInterpolation(1.0f);
    play.scene.UpdateTransforms();
    CHECK(play.scene.GetWorldPosition(box).y == doctest::Approx(currY).epsilon(0.001));
    play.physics->ApplyInterpolation(0.5f);
    play.scene.UpdateTransforms();
    CHECK(play.scene.GetWorldPosition(box).y
          == doctest::Approx((prevY + currY) * 0.5f).epsilon(0.001));
}

TEST_CASE("physics.scene: kinematic bodies follow the scene; dynamics rest on them")
{
    PlayScene play;
    const dscene::EntityHandle platform = play.AddBox(0.0f, MotionKind::Kinematic);
    const dscene::EntityHandle rider = play.AddBox(1.5f);
    play.Start();

    // Land the rider on the platform, then slide the platform sideways: the rider rides.
    play.Step(120);
    for (int i = 0; i < 120; ++i)
    {
        Transform t = play.scene.GetLocalTransform(platform);
        t.position.x += 2.0f / 120.0f;   // 2 units over 2 seconds
        play.scene.SetLocalTransform(platform, t);
        play.scene.UpdateTransforms();
        play.Step(1);
    }
    play.physics->ApplyInterpolation(1.0f);
    play.scene.UpdateTransforms();
    CHECK(play.scene.GetWorldPosition(rider).x > 1.0f);   // dragged along by friction
    CHECK(play.scene.GetWorldPosition(rider).y == doctest::Approx(1.0f).epsilon(0.1));   // platform top 0.5 + half extent
}

TEST_CASE("physics.scene: descendant colliders compound into the ancestor body")
{
    PlayScene play;
    (void)play.AddFloor();
    // An L-piece: the body box at origin + a child collider offset +x. Its compound
    // should topple/rest unlike a lone cube - assert the child shape EXISTS by ray.
    const dscene::EntityHandle body = play.AddBox(0.5f, MotionKind::Static);
    dscene::EntityHandle arm = play.scene.CreateEntity(u8"arm");
    play.scene.SetParent(arm, body);
    play.scene.SetLocalPosition(arm, Float3{ 2.0f, 0.0f, 0.0f });
    ColliderComponent& extra = play.scene.GetSystem<ColliderComponentManager>()->Add(arm);
    extra.halfExtents = Float3{ 0.5f, 0.5f, 0.5f };
    play.Start();

    RayHit hit;
    REQUIRE(play.physics->World()->RayCast(Float3{ 2.0f, 5.0f, 0.0f },
                                           Float3{ 0.0f, -1.0f, 0.0f }, 10.0f, hit));
    CHECK(hit.position.y == doctest::Approx(1.0f).epsilon(0.05));   // the arm's top face
}

TEST_CASE("physics.scene: trigger components raise enter events with entity user data")
{
    PlayScene play;
    (void)play.AddFloor();
    const dscene::EntityHandle volume = play.AddBox(2.0f, MotionKind::Kinematic);
    RigidBodyComponent* sensor = play.scene.GetSystem<RigidBodyComponentManager>()->Get(volume);
    sensor->isTrigger = true;
    sensor->halfExtents = Float3{ 1.0f, 1.0f, 1.0f };
    const dscene::EntityHandle faller = play.AddBox(6.0f);
    play.Start();

    bool entered = false;
    const u64 volumeUser = play.scene.GetEntityId(volume).low;
    const u64 fallerUser = play.scene.GetEntityId(faller).low;
    for (int i = 0; i < 240 && !entered; ++i)
    {
        play.Step(1);
        for (const ContactEvent& e : play.physics->Events())
        {
            if (e.kind == ContactKind::TriggerEnter
                && (e.userA == volumeUser || e.userB == volumeUser)
                && (e.userA == fallerUser || e.userB == fallerUser)) { entered = true; }
        }
    }
    CHECK(entered);
}
