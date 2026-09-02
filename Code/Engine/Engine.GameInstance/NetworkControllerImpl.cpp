// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

// Engine::GameInstance :networkcontroller - the networking role bodies.
// A second implementation unit of engine.gameinstance; it sees
// NetworkController through the primary interface's `export import :networkcontroller`.

module;
#include "Core/Prelude.h"
#include "Core/Log/Log.h"

module engine.gameinstance;

import foundation.core;
import foundation.net.manager; // NetworkManager factories (HostServer/JoinServer)

using namespace foundation::core;

namespace engine::runtime
{
    namespace net = foundation::net;

    bool NetworkController::StartServer(u16 port, bool dedicated)
    {
        // Fixed-port hosts ALSO open the browser gateway on port + 1 by convention (a web
        // client joins ws://host:port+1) - zero script-API churn, and the trust domain is
        // the same localhost/LAN one the UDP listener already accepts. OS-assigned-port
        // hosts (tests/tools) stay UDP-only.
        const u16 webSocketPort =
            (port != 0 && port != 0xFFFF) ? static_cast<u16>(port + 1) : u16{0};
        m_net = net::NetworkManager::HostServer(port, dedicated, {}, webSocketPort);
        if (!m_net)
        {
            LOG_ERROR(u8"App", u8"failed to open a server socket on port {}", port);
            return false;
        }
        m_net->SetReplicatedScene(m_scene);
        WireSceneSystem(m_net.Get()); // the replicated scene's fixed lane now drives this endpoint
        if (m_spawnResolverFactory)
        {
            m_net->Replication().SetSpawnHandler(m_spawnResolverFactory()); // controller owns the wiring
        }
        if (m_net->WebSocketBoundPort() != 0)
        {
            LOG_INFO(u8"App", u8"server listening on port {} (browser gateway ws://:{})",
                     m_net->BoundPort(), m_net->WebSocketBoundPort());
        }
        else
        {
            LOG_INFO(u8"App", u8"server listening on port {}", m_net->BoundPort());
        }
        return true;
    }

    bool NetworkController::Connect(core::StringView host, u16 port)
    {
#ifdef __EMSCRIPTEN__
        // The SAME game script joins with the UDP port on every platform; the browser build
        // redirects to the host's gateway (port + 1 - the StartServer convention above).
        if (port != 0 && port != 0xFFFF)
        {
            port = static_cast<u16>(port + 1);
        }
#endif
        m_net = net::NetworkManager::JoinServer(host, port);
        if (!m_net)
        {
            LOG_ERROR(u8"App", u8"failed to open a client socket");
            return false;
        }
        m_net->SetReplicatedScene(m_scene);
        WireSceneSystem(m_net.Get()); // the replicated scene's fixed lane now drives this endpoint
        if (m_spawnResolverFactory)
        {
            m_net->Replication().SetSpawnHandler(m_spawnResolverFactory()); // controller owns the wiring
        }
        LOG_INFO(u8"App", u8"connecting to {}:{}", host, port);
        return true;
    }

    void NetworkController::StopNetworking()
    {
        WireSceneSystem(nullptr); // detach the scene system BEFORE the endpoint dies (no dangling driver)
        if (m_net)
        {
            LOG_INFO(u8"App", u8"networking stopped");
        }
        m_net = nullptr; // closes the session (drops peers) + the owned socket
    }

    // The transport pump lives on engine::net::NetworkSubsystem::PostUpdate - no DriveNetwork here.
}
