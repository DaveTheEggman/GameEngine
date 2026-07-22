// draconic.net.subsystem - NetworkSubsystem injects the NetworkComponentManager into scenes so
// authored NetworkComponents (and the server's runtime AssignNetworkId) have a home.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.scene;
import draconic.net.replication;   // NetworkComponentManager identity ("net.Network")
import draconic.net.subsystem;

using namespace draconic::core;
namespace net = draconic::net;
namespace dscene = draconic::scene;

TEST_CASE("net-subsystem: OnSceneCreated injects the NetworkComponentManager")
{
    net::RegisterReplicationComponents();

    net::NetworkSubsystem subsystem;
    dscene::Scene scene;

    // No net manager until the subsystem injects it (a bare scene is not networked).
    CHECK(scene.FindManagerBySerializationId(u8"net.Network") == nullptr);

    subsystem.OnSceneCreated(scene);   // the ISceneAware pass-1 injection the SceneManager fans out

    // Now an authored/assigned NetworkComponent has a home in this scene.
    CHECK(scene.FindManagerBySerializationId(u8"net.Network") != nullptr);
}
