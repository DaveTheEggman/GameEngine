// draconic.net.subsystem - the runtime networking home (NetSubsystem) mechanics + facade registration.
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import draconic.core;
import draconic.net;
import draconic.net.subsystem;

using namespace draconic::core;
namespace net = draconic::net;

TEST_CASE("net-subsystem: server + client connect, and an RPC routes through the subsystem")
{
    net::SimConditions sim; sim.latencyMs = 20.0f; sim.lossPct = 0.2f; sim.seed = 3;
    net::SimDatagramNetwork network(sim);
    net::IDatagramSocket* sv = network.CreateSocket();
    net::NetSubsystem server(*sv);
    net::NetSubsystem client(*network.CreateSocket());

    server.StartServer(/*dedicated=*/true);
    const net::PeerId sp = client.ConnectTo(sv->LocalEndpoint());
    CHECK(server.Session().IsServer());
    CHECK(client.Session().IsClient());

    // A server-side order handler, driven by the subsystem's own RPC pump in Update.
    bool got = false; f64 arg = 0.0; net::PeerId from = net::kInvalidPeer;
    server.Rpc().On(u8"order", [&](net::PeerId sender, net::BitReader& r) {
        const u64 bits = r.ReadU64(); MemCopy(&arg, &bits, sizeof(arg));
        from = sender; got = true;
    });

    for (int i = 0; i < 40; ++i) { network.Advance(10.0f); server.Update(10.0f); client.Update(10.0f); }
    CHECK(server.Session().PeerCount() == 1u);

    // Client fires an order via its subsystem's RPC table.
    client.Rpc().Call(client.Session(), sp, u8"order", [](net::BitWriter& w) {
        const f64 v = 42.5; u64 b = 0; MemCopy(&b, &v, sizeof(b)); w.WriteU64(b);
    });

    for (int i = 0; i < 200 && !got; ++i) { network.Advance(10.0f); server.Update(10.0f); client.Update(10.0f); }
    CHECK(got);
    CHECK(arg == doctest::Approx(42.5));
    CHECK(from == server.Session().Peers()[0].id);
}

TEST_CASE("net-subsystem: the Net facade type registers")
{
    net::RegisterNetScriptFacade();   // idempotent; must not crash + registers the reflected type
    const TypeInfo& ti = net::Net::StaticType();
    CHECK(PropertyCount(ti) == 0u);   // a facade has methods, not properties
    // (End-to-end script binding on both backends is exercised in FacadeScriptTests.cpp.)
}

TEST_CASE("net-startup: role=None yields an inactive runtime (single-player)")
{
    net::NetworkStartup cfg;   // role defaults to None
    net::NetworkRuntime rt = net::StartNetworking(cfg);
    CHECK_FALSE(rt.IsActive());
    CHECK(rt.socket.Get() == nullptr);
    CHECK(rt.subsystem.Get() == nullptr);
}

TEST_CASE("net-startup: config -> socket -> role connects a server + client over real localhost UDP")
{
    net::NetworkStartup serverCfg;
    serverCfg.role = net::NetworkRole::Server;
    serverCfg.dedicated = true;
    serverCfg.listenPort = 0;   // OS-assigned
    net::NetworkRuntime server = net::StartNetworking(serverCfg);
    REQUIRE(server.IsActive());
    REQUIRE(server.socket->IsOpen());
    CHECK(server.subsystem->Session().IsServer());

    net::NetworkStartup clientCfg;
    clientCfg.role = net::NetworkRole::Client;
    clientCfg.serverHost = String(u8"127.0.0.1");
    clientCfg.serverPort = server.socket->BoundPort();   // connect to the server's actual port
    net::NetworkRuntime client = net::StartNetworking(clientCfg);
    REQUIRE(client.IsActive());
    CHECK(client.subsystem->Session().IsClient());

    // Drive both on the fixed lane (16 ms) until the handshake lands.
    for (int i = 0; i < 300 && server.subsystem->Session().PeerCount() == 0u; ++i)
    {
        server.subsystem->Update(16.0f);
        client.subsystem->Update(16.0f);
    }
    CHECK(server.subsystem->Session().PeerCount() == 1u);
}
