/// Draconic::Net - `draconic.net:socket` partition.
///
/// The REAL UDP IDatagramSocket backend: wraps the Core/System socket primitives (docs/design/
/// networking.md §3.1 - sockets live in Core/System) so ReliableTransport, proven against the
/// deterministic sim, runs over an actual network with zero protocol changes. IPv4 for v1; a
/// DatagramEndpoint packs (ip << 16) | port in host order.

module;
#include "Core/Prelude.h"

export module draconic.net:socket;

import draconic.core;
import :datagram;

using namespace draconic::core;

export namespace draconic::net {

// Pack/unpack an IPv4 DatagramEndpoint (value = (ip << 16) | port, host order).
[[nodiscard]] inline DatagramEndpoint MakeEndpoint(u32 ip, u16 port) noexcept {
    return DatagramEndpoint{ (static_cast<u64>(ip) << 16) | static_cast<u64>(port) };
}
[[nodiscard]] inline u32 EndpointIp(const DatagramEndpoint& e) noexcept { return static_cast<u32>(e.value >> 16); }
[[nodiscard]] inline u16 EndpointPort(const DatagramEndpoint& e) noexcept { return static_cast<u16>(e.value & 0xFFFFull); }

// Parse "a.b.c.d" + port into an endpoint; an invalid (value 0) endpoint on parse failure.
[[nodiscard]] inline DatagramEndpoint ResolveEndpoint(StringView dottedQuad, u16 port) {
    u32 ip = 0;
    if (!core::ParseIPv4(dottedQuad, ip)) { return DatagramEndpoint{}; }
    return MakeEndpoint(ip, port);
}

// A real UDP socket presented as an IDatagramSocket. Non-blocking; owns one Core/System handle and
// a refcount on the platform networking layer.
class UdpDatagramSocket final : public IDatagramSocket {
public:
    // Bind to `port` (0 = OS-assigned). Check IsOpen() afterwards.
    explicit UdpDatagramSocket(u16 port = 0) {
        core::InitializeNetworking();
        m_handle = core::UdpOpen(port, &m_boundPort);
    }
    ~UdpDatagramSocket() override {
        if (m_handle != core::kInvalidSocket) { core::SocketClose(m_handle); }
        core::ShutdownNetworking();
    }
    UdpDatagramSocket(const UdpDatagramSocket&) = delete;
    UdpDatagramSocket& operator=(const UdpDatagramSocket&) = delete;

    [[nodiscard]] bool IsOpen() const noexcept { return m_handle != core::kInvalidSocket; }
    [[nodiscard]] u16 BoundPort() const noexcept { return m_boundPort; }

    void Send(const DatagramEndpoint& to, Span<const byte> data) override {
        if (m_handle == core::kInvalidSocket) { return; }
        (void)core::UdpSendTo(m_handle, EndpointIp(to), EndpointPort(to), data.Data(), data.Size());
    }

    [[nodiscard]] bool Receive(DatagramEndpoint& from, Array<byte>& out) override {
        if (m_handle == core::kInvalidSocket) { return false; }
        out.Resize(kMaxDatagram);
        u32 ip = 0; u16 port = 0;
        const i64 n = core::UdpRecvFrom(m_handle, out.Data(), out.Size(), ip, port);
        if (n <= 0) { out.Clear(); return false; }
        out.Resize(static_cast<usize>(n));
        from = MakeEndpoint(ip, port);
        return true;
    }

    // For a real socket, "local endpoint" = loopback + the bound port - the address a peer on the
    // same host connects to. (A real remote uses the public/LAN ip; resolve it via ResolveEndpoint.)
    [[nodiscard]] DatagramEndpoint LocalEndpoint() const override {
        u32 loopback = 0;
        (void)core::ParseIPv4(u8"127.0.0.1", loopback);
        return MakeEndpoint(loopback, m_boundPort);
    }

private:
    static constexpr usize kMaxDatagram = 2048;   // > the reliable transport's maxPacketBytes
    core::SocketHandle m_handle = core::kInvalidSocket;
    u16 m_boundPort = 0;
};

// --- TCP (stream) sockets ---------------------------------------------------------------------
// RAII over the Core/System TCP primitive, for the future draconic.http / WebSocket / debugger
// transports. Each socket holds one WSA refcount (no-op on POSIX), transferred on move.

// A connected TCP stream (a client connection, or one accepted by a TcpListener). Movable, RAII.
class TcpSocket {
public:
    TcpSocket() = default;
    // Adopt a handle; `ownsNet` = this instance holds a networking refcount to release on close.
    TcpSocket(core::SocketHandle handle, bool ownsNet) noexcept : m_handle(handle), m_ownsNet(ownsNet) {}
    ~TcpSocket() { Close(); }
    TcpSocket(TcpSocket&& o) noexcept : m_handle(o.m_handle), m_ownsNet(o.m_ownsNet) { o.m_handle = core::kInvalidSocket; o.m_ownsNet = false; }
    TcpSocket& operator=(TcpSocket&& o) noexcept {
        if (this != &o) { Close(); m_handle = o.m_handle; m_ownsNet = o.m_ownsNet; o.m_handle = core::kInvalidSocket; o.m_ownsNet = false; }
        return *this;
    }
    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    // Begin a NON-BLOCKING connect to (dottedQuad, port). Poll ConnectStatus() until 1 (connected)
    // or -1 (failed). !IsOpen() if the address was unparseable.
    [[nodiscard]] static TcpSocket Connect(StringView dottedQuad, u16 port) {
        core::InitializeNetworking();
        u32 ip = 0;
        if (!core::ParseIPv4(dottedQuad, ip)) { core::ShutdownNetworking(); return TcpSocket{}; }
        return TcpSocket(core::TcpConnect(ip, port), /*ownsNet=*/true);
    }

    [[nodiscard]] bool IsOpen() const noexcept { return m_handle != core::kInvalidSocket; }
    // 1 = connected, 0 = still connecting, -1 = failed.
    [[nodiscard]] int ConnectStatus() const noexcept { return core::TcpConnectStatus(m_handle); }
    // Bytes sent (may be partial), 0 = would-block (retry), -1 = closed/error.
    [[nodiscard]] i64 Send(Span<const byte> data) noexcept { return core::TcpSend(m_handle, data.Data(), data.Size()); }
    // Bytes read (>0), 0 = would-block (no data yet), -1 = closed/error.
    [[nodiscard]] i64 Receive(Span<byte> out) noexcept { return core::TcpRecv(m_handle, out.Data(), out.Size()); }
    void Close() noexcept {
        if (m_handle != core::kInvalidSocket) { core::SocketClose(m_handle); m_handle = core::kInvalidSocket; }
        if (m_ownsNet) { core::ShutdownNetworking(); m_ownsNet = false; }
    }
    [[nodiscard]] core::SocketHandle Handle() const noexcept { return m_handle; }

private:
    core::SocketHandle m_handle = core::kInvalidSocket;
    bool m_ownsNet = false;
};

// A TCP listening socket; Accept() returns pending client connections (non-blocking).
class TcpListener {
public:
    explicit TcpListener(u16 port = 0) {
        core::InitializeNetworking();
        m_handle = core::TcpListen(port, &m_boundPort);
    }
    ~TcpListener() {
        if (m_handle != core::kInvalidSocket) { core::SocketClose(m_handle); }
        core::ShutdownNetworking();
    }
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    [[nodiscard]] bool IsOpen() const noexcept { return m_handle != core::kInvalidSocket; }
    [[nodiscard]] u16 BoundPort() const noexcept { return m_boundPort; }
    // Accept one pending connection; the returned socket is !IsOpen() when none is pending.
    [[nodiscard]] TcpSocket Accept() {
        const core::SocketHandle h = core::TcpAccept(m_handle, nullptr, nullptr);
        if (h == core::kInvalidSocket) { return TcpSocket{}; }
        core::InitializeNetworking();   // independent refcount for the accepted socket
        return TcpSocket(h, /*ownsNet=*/true);
    }

private:
    core::SocketHandle m_handle = core::kInvalidSocket;
    u16 m_boundPort = 0;
};

}
