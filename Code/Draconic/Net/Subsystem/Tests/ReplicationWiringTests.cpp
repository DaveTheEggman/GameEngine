// draconic.net.subsystem - StateReplication driven end-to-end through NetSubsystem over the sim
// transport: a server-assigned networked entity's replicated state reaches a connected client's scene.
#include <doctest/doctest.h>
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

import draconic.core;
import draconic.net;
import draconic.net.replication;
import draconic.net.subsystem;
import draconic.scene;

using namespace draconic::core;
namespace net = draconic::net;
namespace dscene = draconic::scene;

namespace
{
    struct RepMover { Float3 position{ 0, 0, 0 }; i32 health = 0; };

    inline void Serialize(ISerializer& ar, RepMover& m)
    {
        draconic::core::Serialize(ar, "position", m.position);
        draconic::core::Serialize(ar, "health", m.health);
    }

    class RepMoverManager final : public dscene::SerializableComponentManager<RepMover>
    {
    public:
        RepMoverManager() : SerializableComponentManager(u8"test.RepMover") {}
    };
}

DRACONIC_REFLECT_VALUE(RepMover, "draconic::net::test")
{
    builder.Property<&RepMover::position>("position").PropAttribute(net::kReplicatedAttribute, true);
    builder.Property<&RepMover::health>("health").PropAttribute(net::kReplicatedAttribute, true);
}

TEST_CASE("net-subsystem: state replicates server -> client through the subsystem + transport")
{
    DraconicRegisterValue_RepMover();
    net::RegisterReplicationComponents();

    net::SimConditions sim; sim.latencyMs = 15.0f; sim.lossPct = 0.1f; sim.seed = 7;
    net::SimDatagramNetwork network(sim);
    net::IDatagramSocket* sv = network.CreateSocket();
    net::NetSubsystem server(*sv);
    net::NetSubsystem client(*network.CreateSocket());

    dscene::Scene serverScene;
    serverScene.AddSystem<net::NetworkComponentManager>();
    RepMoverManager* serverMovers = serverScene.AddSystem<RepMoverManager>();
    server.SetReplicatedScene(&serverScene);

    dscene::Scene clientScene;
    clientScene.AddSystem<net::NetworkComponentManager>();
    clientScene.AddSystem<RepMoverManager>();
    client.SetReplicatedScene(&clientScene);

    server.StartServer(/*dedicated=*/true);
    (void)client.ConnectTo(sv->LocalEndpoint());

    // Handshake.
    for (int i = 0; i < 60 && server.Session().PeerCount() == 0u; ++i) {
        network.Advance(10.0f); server.Update(10.0f); client.Update(10.0f);
    }
    REQUIRE(server.Session().PeerCount() == 1u);

    // Server spawns a networked entity with replicated state.
    const dscene::EntityHandle e = serverScene.CreateEntity(u8"Unit");
    RepMover& m = serverMovers->Add(e);
    m.position = Float3{ 3, 0, -2 }; m.health = 42;
    const net::NetworkId id = server.Replication().AssignNetworkId(serverScene, e);
    REQUIRE(id.IsValid());

    // Drive until the client's scene mirrors it (reliable-ordered => converges).
    dscene::EntityHandle ce = dscene::EntityHandle::Invalid();
    for (int i = 0; i < 400; ++i) {
        network.Advance(10.0f); server.Update(10.0f); client.Update(10.0f);
        ce = client.Replication().FindEntity(id);
        if (clientScene.IsValid(ce) && clientScene.GetSystem<RepMoverManager>()->Get(ce) != nullptr) { break; }
    }
    REQUIRE(clientScene.IsValid(ce));
    const RepMover* rc = clientScene.GetSystem<RepMoverManager>()->Get(ce);
    REQUIRE(rc != nullptr);
    CHECK(rc->health == 42);
    CHECK(rc->position == Float3{ 3, 0, -2 });

    // A server-side change propagates on the next ticks.
    m.health = 99;
    for (int i = 0; i < 200 && rc->health != 99; ++i) {
        network.Advance(10.0f); server.Update(10.0f); client.Update(10.0f);
        rc = clientScene.GetSystem<RepMoverManager>()->Get(ce);
    }
    CHECK(rc->health == 99);
}
