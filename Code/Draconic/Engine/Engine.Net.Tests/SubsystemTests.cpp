// engine.net - NetworkSubsystem injects the NetworkComponentManager into scenes so
// authored NetworkComponents (and the server's runtime AssignNetworkId) have a home.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.scene;
import foundation.net.replication; // NetworkComponentManager identity ("net.Network")
import engine.net;

using namespace foundation::core;
namespace scene = foundation::scene;

TEST_CASE("net-subsystem: OnSceneCreated injects the NetworkComponentManager")
{
    foundation::net::RegisterReplicationComponents();

    engine::net::NetworkSubsystem subsystem;
    scene::Scene scene;

    // No net managers until the subsystem injects them (a bare scene is not networked).
    CHECK(scene.FindManagerBySerializationId(u8"net.Network") == nullptr);
    CHECK(scene.FindManagerBySerializationId(u8"net.Transform") == nullptr);

    subsystem.OnSceneCreated(scene); // the ISceneAware pass-1 injection the SceneManager fans out

    // Now an authored/assigned NetworkComponent (identity) + NetworkedTransform (replicated movement)
    // both have a home in this scene.
    CHECK(scene.FindManagerBySerializationId(u8"net.Network") != nullptr);
    CHECK(scene.FindManagerBySerializationId(u8"net.Transform") != nullptr);
}
