// Draconic::NetworkManager - implementation unit: the DRACONIC_REFLECT_* body for the Net facade +
// the registration (kept out of the interface unit per the GCC gcm-cluster rule).

module;
#include "Core/Prelude.h"
#include "Core/Reflection/Reflect.h"

module draconic.net.manager;

import draconic.core;
import draconic.net;
import draconic.script;
import draconic.script.facades;   // RegisterExtraFacadeName (the Wren prelude hook)

using namespace draconic::core;
using namespace draconic::script;

namespace draconic::net
{
    DRACONIC_REFLECT(Net, "draconic::net")
    {
        builder.Method<&Net::isServer>("isServer");
        builder.Method<&Net::isClient>("isClient");
        builder.Method<&Net::peerCount>("peerCount");
        builder.Method<&Net::networkTick>("networkTick");
        builder.Method<&Net::networkTimeMs>("networkTimeMs");
        builder.Method<&Net::rpc>("rpc");
        builder.Method<&Net::rpcNumber>("rpcNumber");
        builder.Method<&Net::rpcText>("rpcText");
        builder.Constructor();   // Wren only materializes constructible foreign classes
    }

    void RegisterNetScriptFacade()
    {
        static const bool once = []() {
            GlobalTypeRegistry().Register(Net::StaticType());
            RegisterExtraFacadeName(u8"Net");   // so the Wren behavior prelude imports it (AngelScript binds by registry)
            return true;
        }();
        (void)once;
    }

    NetworkRuntime StartNetworking(const NetworkStartup& config)
    {
        NetworkRuntime runtime;
        if (config.role == NetworkRole::None) { return runtime; }

        // The facade is bound wherever networking actually runs (idempotent).
        RegisterNetScriptFacade();

        // Server binds the listen port; a client binds ephemeral (0) unless a port is forced.
        runtime.socket = MakeUnique<UdpSocket>(DefaultAllocator(), config.listenPort);
        runtime.manager = MakeUnique<NetworkManager>(DefaultAllocator(), *runtime.socket, config.reliable);

        if (config.role == NetworkRole::Server)
        {
            runtime.manager->StartServer(config.dedicated);
        }
        else   // Client
        {
            const DatagramEndpoint server = ResolveEndpoint(config.serverHost.AsView(), config.serverPort);
            runtime.manager->ConnectTo(server);
        }
        return runtime;
    }
}
