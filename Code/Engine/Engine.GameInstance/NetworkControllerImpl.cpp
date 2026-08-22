// Engine::GameInstance :networkcontroller - the role bodies moved verbatim off GameInstance
// (networking-extraction.md P1). A second implementation unit of engine.gameinstance; it sees
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
        m_net = net::NetworkManager::HostServer(port, dedicated);
        if (!m_net)
        {
            LOG_ERROR(u8"App", u8"failed to open a server socket on port {}", port);
            return false;
        }
        m_net->SetReplicatedScene(m_scene);
        if (m_onEndpointOnline)
        {
            m_onEndpointOnline(*m_net);
        } // app wires per-endpoint setup (spawn resolver)
        LOG_INFO(u8"App", u8"server listening on port {}", m_net->BoundPort());
        return true;
    }

    bool NetworkController::Connect(core::StringView host, u16 port)
    {
        m_net = net::NetworkManager::JoinServer(host, port);
        if (!m_net)
        {
            LOG_ERROR(u8"App", u8"failed to open a client socket");
            return false;
        }
        m_net->SetReplicatedScene(m_scene);
        if (m_onEndpointOnline)
        {
            m_onEndpointOnline(*m_net);
        }
        LOG_INFO(u8"App", u8"connecting to {}:{}", host, port);
        return true;
    }

    void NetworkController::StopNetworking()
    {
        if (m_net)
        {
            LOG_INFO(u8"App", u8"networking stopped");
        }
        m_net = nullptr; // closes the session (drops peers) + the owned socket
    }

    void NetworkController::DriveNetwork(f32 fixedDeltaMs)
    {
        if (m_net)
        {
            m_net->Update(fixedDeltaMs);
        }
    }
}
