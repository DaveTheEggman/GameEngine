// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Net - `foundation.net:tcp_socket` partition.
///
/// RAII TCP stream sockets over the Core/System TCP primitive, for
/// the foundation.http / WebSocket / script-debugger transports - NOT the UDP game transport.
/// Each socket holds one WSA refcount (a no-op on POSIX), transferred on move. IPv4 only.

module;
#include "Core/Prelude.h"

export module foundation.net:tcp_socket;

import foundation.core;

using namespace foundation::core;
namespace core = foundation::core;

export namespace foundation::net
{

    // A connected TCP stream (a client connection, or one accepted by a TcpListener). Movable, RAII.
    class TcpSocket
    {
    public:
        TcpSocket() = default;
        // Adopt a handle; `ownsNet` = this instance holds a networking refcount to release on close.
        TcpSocket(core::SocketHandle handle, bool ownsNet) noexcept
            : m_handle(handle), m_ownsNet(ownsNet)
        {
        }
        ~TcpSocket() { Close(); }
        TcpSocket(TcpSocket&& o) noexcept : m_handle(o.m_handle), m_ownsNet(o.m_ownsNet)
        {
            o.m_handle = core::kInvalidSocket;
            o.m_ownsNet = false;
        }
        TcpSocket& operator=(TcpSocket&& o) noexcept
        {
            if (this != &o)
            {
                Close();
                m_handle = o.m_handle;
                m_ownsNet = o.m_ownsNet;
                o.m_handle = core::kInvalidSocket;
                o.m_ownsNet = false;
            }
            return *this;
        }
        TcpSocket(const TcpSocket&) = delete;
        TcpSocket& operator=(const TcpSocket&) = delete;

        // Begin a NON-BLOCKING connect to (dottedQuad, port). Poll ConnectStatus() until 1 (connected)
        // or -1 (failed). !IsOpen() if the address was unparseable.
        [[nodiscard]] static TcpSocket Connect(StringView dottedQuad, u16 port)
        {
            core::InitializeNetworking();
            u32 ip = 0;
            if (!core::ParseIPv4(dottedQuad, ip))
            {
                core::ShutdownNetworking();
                return TcpSocket{};
            }
            return TcpSocket(core::TcpConnect(ip, port), /*ownsNet=*/true);
        }

        [[nodiscard]] bool IsOpen() const noexcept { return m_handle != core::kInvalidSocket; }
        // 1 = connected, 0 = still connecting, -1 = failed.
        [[nodiscard]] int ConnectStatus() const noexcept
        {
            return core::TcpConnectStatus(m_handle);
        }
        // Bytes sent (may be partial), 0 = would-block (retry), -1 = closed/error.
        [[nodiscard]] i64 Send(Span<const byte> data) noexcept
        {
            return core::TcpSend(m_handle, data.Data(), data.Size());
        }
        // Bytes read (>0), 0 = would-block (no data yet), -1 = closed/error.
        [[nodiscard]] i64 Receive(Span<byte> out) noexcept
        {
            return core::TcpRecv(m_handle, out.Data(), out.Size());
        }
        void Close() noexcept
        {
            if (m_handle != core::kInvalidSocket)
            {
                core::SocketClose(m_handle);
                m_handle = core::kInvalidSocket;
            }
            if (m_ownsNet)
            {
                core::ShutdownNetworking();
                m_ownsNet = false;
            }
        }
        [[nodiscard]] core::SocketHandle Handle() const noexcept { return m_handle; }

    private:
        core::SocketHandle m_handle = core::kInvalidSocket;
        bool m_ownsNet = false;
    };

    // A TCP listening socket; Accept() returns pending client connections (non-blocking).
    class TcpListener
    {
    public:
        explicit TcpListener(u16 port = 0)
        {
            core::InitializeNetworking();
            m_handle = core::TcpListen(port, &m_boundPort);
        }
        ~TcpListener()
        {
            if (m_handle != core::kInvalidSocket)
            {
                core::SocketClose(m_handle);
            }
            core::ShutdownNetworking();
        }
        TcpListener(const TcpListener&) = delete;
        TcpListener& operator=(const TcpListener&) = delete;

        [[nodiscard]] bool IsOpen() const noexcept { return m_handle != core::kInvalidSocket; }
        [[nodiscard]] u16 BoundPort() const noexcept { return m_boundPort; }
        // Accept one pending connection; the returned socket is !IsOpen() when none is pending.
        [[nodiscard]] TcpSocket Accept()
        {
            const core::SocketHandle h = core::TcpAccept(m_handle, nullptr, nullptr);
            if (h == core::kInvalidSocket)
            {
                return TcpSocket{};
            }
            core::InitializeNetworking(); // independent refcount for the accepted socket
            return TcpSocket(h, /*ownsNet=*/true);
        }

    private:
        core::SocketHandle m_handle = core::kInvalidSocket;
        u16 m_boundPort = 0;
    };

}
