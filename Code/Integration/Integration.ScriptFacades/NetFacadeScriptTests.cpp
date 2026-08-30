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
        net::SimDatagramNetwork network{net::SimConditions{}};
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

#ifdef OPTION_HAS_ANGELSCRIPT
TEST_CASE("net-facade: AngelScript reads the live session through the Net facade")
{
    RegisterCoreTypes();
    net::RegisterNetScriptFacade();

    RefPtr<IScriptManager> manager = angelscript::CreateScriptManager();
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

    RefPtr<IScriptManager> manager = CreateLuauScriptManager();
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
