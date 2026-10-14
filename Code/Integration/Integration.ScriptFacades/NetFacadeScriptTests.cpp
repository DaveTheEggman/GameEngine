// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// foundation.net.manager - the Net facade PROVEN end-to-end on both script backends.
//
// ManagerTests.cpp checks the facade TYPE registers; this drives a real AngelScript
// VM: a server manager installs its net.runtime service into the context, then a script calls
// Net.isServer()/isClient()/peerCount() and we read the results back. This is the acceptance test
// for the extensibility hook (registry -> AngelScript).
#include <doctest/doctest.h>
#include "Core/Prelude.h"

import foundation.core;
import foundation.net;
import foundation.net.manager;
import foundation.script;
#ifdef OPTION_HAS_ANGELSCRIPT
import foundation.script.angelscript;
#endif
#ifdef OPTION_HAS_LUAU
import foundation.script.luau;
#endif

using namespace foundation::core;
using namespace foundation::script;
namespace net = foundation::net;

namespace
{
    // A server endpoint over the sim network + a controller the Net facade resolves through. The
    // endpoint is already live here, so the runtime role hooks are no-ops (they are exercised
    // per-instance in the runtime tests); NetEndpoint() hands the facade the live server.
    struct ServerFixture final : net::INetworkController
    {
        net::SimDatagramNetwork network{foundation::core::DefaultAllocator(),
                                        net::SimConditions{}};
        net::NetworkManager server{*network.CreateSocket()};
        net::NetScriptBinding binding;
        ServerFixture()
        {
            server.StartServer(/*dedicated=*/true);
            binding.controller = this;
        }

        bool StartServer(u16, bool) override { return true; }
        bool Connect(StringView, u16) override { return false; }
        void StopNetworking() override {}
        [[nodiscard]] net::NetworkManager* NetEndpoint() const override
        {
            return const_cast<net::NetworkManager*>(&server);
        }
    };
}

namespace
{
    // A controller over an endpoint that may not exist yet (Net.on before hosting or joining).
    struct PeerController final : net::INetworkController
    {
        net::NetworkManager* endpoint = nullptr;
        net::NetScriptBinding binding;
        PeerController() { binding.controller = this; }
        bool StartServer(u16, bool) override { return false; }
        bool Connect(StringView, u16) override { return false; }
        void StopNetworking() override {}
        [[nodiscard]] net::NetworkManager* NetEndpoint() const override { return endpoint; }
    };

    // A host and a client over the sim network, each with its own controller (and so its own
    // script context's binding). The host's controller has no endpoint until Open: its script
    // registers Net.on first, as a game does before it hosts.
    struct HostAndClient
    {
        net::SimDatagramNetwork network{foundation::core::DefaultAllocator(), net::SimConditions{}};
        net::IDatagramSocket* hostSocket = network.CreateSocket();
        net::NetworkManager host{*hostSocket};
        net::NetworkManager client{*network.CreateSocket()};
        PeerController hostController;
        PeerController clientController;

        void Open()
        {
            host.StartServer(/*dedicated=*/false);
            hostController.endpoint = &host;
            net::InstallScriptRpcHandlers(hostController.binding, host); // what the controller does
            (void)client.ConnectTo(hostSocket->LocalEndpoint());
            clientController.endpoint = &client;
            net::InstallScriptRpcHandlers(clientController.binding, client);
            Pump(40);
            REQUIRE(host.Session().PeerCount() == 1u);
        }

        void Pump(int steps)
        {
            for (int i = 0; i < steps; ++i)
            {
                network.Advance(10.0f);
                host.Update(10.0f);
                client.Update(10.0f);
            }
        }

        [[nodiscard]] f64 ClientPeerOnHost() { return static_cast<f64>(host.Session().Peers()[0].id); }
    };
}

#ifdef OPTION_HAS_ANGELSCRIPT
TEST_CASE("net-facade: AngelScript Net.on receives a number, a text and a bare RPC, and replies")
{
    RegisterCoreTypes();
    net::RegisterNetScriptFacade();
    RefPtr<IScriptManager> manager = angelscript::CreateScriptManager(foundation::core::DefaultAllocator());
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> hostCtx = manager->CreateContext();
    RefPtr<IScriptContext> clientCtx = manager->CreateContext();
    HostAndClient fx;
    net::InstallNetScriptService(*hostCtx, fx.hostController.binding);
    net::InstallNetScriptService(*clientCtx, fx.clientController.binding);

    // The host registers before it hosts; its number handler answers the sender at once.
    REQUIRE(hostCtx
                ->Load(u8"double Score = 0; int ScoreFrom = -1; string Greeting; int Pings = 0;\n"
                       u8"void onScore(int sender, double value) { Score = value; ScoreFrom = sender;\n"
                       u8"  Net::rpcNumber(\"ack\", value * 2); }\n"
                       u8"void onHello(int sender, const string &in text) { Greeting = text; }\n"
                       u8"void onPing(int sender) { Pings++; }\n"
                       u8"void main() {\n"
                       u8"  NetNumberHandler @score = onScore; Net::on(\"score\", score);\n"
                       u8"  NetTextHandler @hello = onHello; Net::on(\"hello\", hello);\n"
                       u8"  NetHandler @ping = onPing; Net::on(\"ping\", ping);\n"
                       u8"}\n",
                       u8"host")
                .IsOk());
    REQUIRE(fx.hostController.binding.rpcHandlers.Size() == 3);
    fx.Open();
    REQUIRE(clientCtx
                ->Load(u8"double Ack = 0;\n"
                       u8"void onAck(int sender, double value) { Ack = value; }\n"
                       u8"void main() {\n"
                       u8"  NetNumberHandler @ack = onAck; Net::on(\"ack\", ack);\n"
                       u8"  Net::rpcNumber(\"score\", 42.5);\n"
                       u8"  Net::rpcText(\"hello\", \"masked\");\n"
                       u8"  Net::rpc(\"ping\"); Net::rpc(\"ping\");\n"
                       u8"}\n",
                       u8"client")
                .IsOk());
    fx.Pump(60);

    CHECK(hostCtx->GetGlobal(u8"Score").Get<f64>() == doctest::Approx(42.5));
    CHECK(hostCtx->GetGlobal(u8"ScoreFrom").Get<f64>() == doctest::Approx(fx.ClientPeerOnHost()));
    CHECK(hostCtx->GetGlobal(u8"Greeting").Get<String>() == StringView(u8"masked"));
    CHECK(hostCtx->GetGlobal(u8"Pings").Get<f64>() == doctest::Approx(2.0));
    CHECK(clientCtx->GetGlobal(u8"Ack").Get<f64>() == doctest::Approx(85.0)); // the reply, from inside a handler
}
#endif // OPTION_HAS_ANGELSCRIPT

#ifdef OPTION_HAS_LUAU
TEST_CASE("net-facade: Luau Net.on receives a number, a text and a bare RPC, and replies")
{
    RegisterCoreTypes();
    net::RegisterNetScriptFacade();
    RefPtr<IScriptManager> manager = CreateLuauScriptManager(DefaultAllocator());
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> hostCtx = manager->CreateContext();
    RefPtr<IScriptContext> clientCtx = manager->CreateContext();
    HostAndClient fx;
    net::InstallNetScriptService(*hostCtx, fx.hostController.binding);
    net::InstallNetScriptService(*clientCtx, fx.clientController.binding);

    REQUIRE(hostCtx
                ->Load(u8"Score = 0; ScoreFrom = -1; Greeting = ''; Pings = 0\n"
                       u8"Net.on('score', function(sender, value) Score = value; ScoreFrom = sender\n"
                       u8"  Net.rpcNumber('ack', value * 2) end)\n"
                       u8"Net.on('hello', function(sender, text) Greeting = text end)\n"
                       u8"Net.on('ping', function(sender) Pings = Pings + 1 end)\n",
                       u8"host")
                .IsOk());
    fx.Open();
    REQUIRE(clientCtx
                ->Load(u8"Ack = 0\n"
                       u8"Net.on('ack', function(sender, value) Ack = value end)\n"
                       u8"Net.rpcNumber('score', 42.5)\n"
                       u8"Net.rpcText('hello', 'masked')\n"
                       u8"Net.rpc('ping'); Net.rpc('ping')\n",
                       u8"client")
                .IsOk());
    fx.Pump(60);

    CHECK(hostCtx->GetGlobal(u8"Score").Get<f64>() == doctest::Approx(42.5));
    CHECK(hostCtx->GetGlobal(u8"ScoreFrom").Get<f64>() == doctest::Approx(fx.ClientPeerOnHost()));
    CHECK(hostCtx->GetGlobal(u8"Greeting").Get<String>() == StringView(u8"masked"));
    CHECK(hostCtx->GetGlobal(u8"Pings").Get<f64>() == doctest::Approx(2.0));
    CHECK(clientCtx->GetGlobal(u8"Ack").Get<f64>() == doctest::Approx(85.0));
}
#endif // OPTION_HAS_LUAU

TEST_CASE("net-facade: a truncated script RPC never reaches the handler")
{
    // A raw RPC carrying the number kind and no number: the handler is not called.
    HostAndClient fx;
    struct Counting final : IScriptDelegate
    {
        int calls = 0;
        Result<Variant> Invoke(Span<Variant>) override
        {
            ++calls;
            return Variant{};
        }
    };
    RefPtr<Counting> counting = MakeRef<Counting>(DefaultAllocator());
    fx.Open();
    net::Net::InstallScriptRpcHandler(fx.host, u8"score", RefPtr<IScriptDelegate>(counting.Get()));
    fx.client.Rpc().Call(fx.client.Session(), fx.client.Session().ServerPeer(), u8"score",
                         [](net::BitWriter& w) { w.WriteU8(static_cast<u8>(net::ScriptRpcKind::Number)); });
    fx.Pump(40);
    CHECK(counting->calls == 0);
    fx.client.Rpc().Call(fx.client.Session(), fx.client.Session().ServerPeer(), u8"score",
                         [](net::BitWriter& w) { w.WriteU8(static_cast<u8>(net::ScriptRpcKind::None)); });
    fx.Pump(40);
    CHECK(counting->calls == 1);
}

#ifdef OPTION_HAS_ANGELSCRIPT
TEST_CASE("net-facade: AngelScript reads the live session through the Net facade")
{
    RegisterCoreTypes();
    net::RegisterNetScriptFacade();

    RefPtr<IScriptManager> manager = angelscript::CreateScriptManager(foundation::core::DefaultAllocator());
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    ServerFixture fx;
    net::InstallNetScriptService(*ctx, fx.binding);

    // AngelScript: statics live in the type's namespace (Net::isServer()); logic in main().
    const Status status = ctx->Load(u8"bool IsServer;\n"
                                    u8"bool IsClient;\n"
                                    u8"int Peers;\n"
                                    u8"void main() {\n"
                                    u8"  IsServer = Net::isServer();\n"
                                    u8"  IsClient = Net::isClient();\n"
                                    u8"  Peers = Net::peerCount();\n"
                                    u8"}\n",
                                    u8"main");
    REQUIRE(status.IsOk());

    CHECK(ctx->GetGlobal(u8"IsServer").Get<bool>() == true);
    CHECK(ctx->GetGlobal(u8"IsClient").Get<bool>() == false);
    CHECK(ctx->GetGlobal(u8"Peers").Get<f64>() ==
          0.0); // the AS backend unifies integer globals to f64
}
#endif // OPTION_HAS_ANGELSCRIPT

#ifdef OPTION_HAS_LUAU
TEST_CASE("net-facade: Luau reads the live session through the Net facade")
{
    RegisterCoreTypes();
    net::RegisterNetScriptFacade();

    RefPtr<IScriptManager> manager = CreateLuauScriptManager(DefaultAllocator());
    RegisterReflectedTypes(*manager);
    RefPtr<IScriptContext> ctx = manager->CreateContext();

    ServerFixture fx;
    net::InstallNetScriptService(*ctx, fx.binding);

    // Luau: statics via `.`; the chunk runs at top level, so the globals it assigns are readable.
    const Status status = ctx->Load(u8"IsServer = Net.isServer()\n"
                                    u8"IsClient = Net.isClient()\n"
                                    u8"Peers = Net.peerCount()\n",
                                    u8"main");
    REQUIRE(status.IsOk());

    CHECK(ctx->GetGlobal(u8"IsServer").Get<bool>() == true);
    CHECK(ctx->GetGlobal(u8"IsClient").Get<bool>() == false);
    CHECK(ctx->GetGlobal(u8"Peers").Get<f64>() == 0.0);
}
#endif // OPTION_HAS_LUAU
