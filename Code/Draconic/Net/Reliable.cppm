/// Draconic::Net - `draconic.net:reliable` partition.
///
/// Reliable-UDP over the unreliable datagram substrate (docs/design/networking.md §4). A
/// `ReliableTransport` (an INetTransport) manages per-remote connections over an IDatagramSocket and
/// layers on: a packet header (protocol id + type + seq/ack/ackBits), ack-driven RTT, RELIABLE
/// messages (re-included in every packet until an acked packet carried them, then delivered IN ORDER
/// by message id), UNRELIABLE pass-through, a connect/accept handshake, and keepalive/timeout. The
/// model (include-all-unacked-reliables-per-packet) is Fiedler-style and ideal for the turn-based
/// target's low message rate. Fragmentation (messages > one datagram) + congestion control are
/// deferred; a message that exceeds the per-packet budget is dropped with a warning.
///
/// Tested headlessly against SimDatagramNetwork's loss/reorder - no OS sockets.

module;
#include "Core/Prelude.h"
#include "Core/Debug/Assert.h"
#include "Core/Log/Log.h"

export module draconic.net:reliable;

import draconic.core;
import :transport;
import :datagram;
import :wire;

using namespace draconic::core;

export namespace draconic::net {

// Tunables for a ReliableTransport (all times in ms).
struct ReliableConfig {
    u16 protocolId        = 0xDCE7;   // reject foreign/mismatched-version packets
    f32 keepAliveMs       = 100.0f;   // send an ack-only packet if idle this long (keeps acks flowing)
    f32 connectResendMs   = 100.0f;   // resend ConnectRequest until accepted
    f32 timeoutMs         = 5000.0f;  // no packet received in this long => disconnect
    u32 maxMessageBytes   = 1024;     // per-message cap (no fragmentation yet)
    u32 maxPacketBytes    = 1200;     // soft budget per datagram (< typical MTU)
};

enum class ConnectionState : u8 { Connecting, Connected, Disconnected };

class ReliableTransport final : public INetTransport {
public:
    ReliableTransport(IDatagramSocket& socket, const ReliableConfig& config = {})
        : m_socket(&socket), m_cfg(config) {}

    // --- connection establishment (driven by the session layer / tests; not part of INetTransport) ---

    // Client: begin connecting to `remote`. Returns the local PeerId (a Connected event fires later).
    PeerId Connect(const DatagramEndpoint& remote) {
        Connection& c = OpenConnection(remote);
        c.state = ConnectionState::Connecting;
        SendConnectRequest(c);
        return c.peer;
    }
    // Server: accept incoming ConnectRequests (default off - a listen server opts in).
    void SetAccepting(bool accepting) noexcept { m_accepting = accepting; }

    // --- INetTransport ---

    void Send(PeerId peer, u8 channel, Span<const byte> data, Reliability reliability) override {
        Connection* c = FindByPeer(peer);
        if (c == nullptr || c->state == ConnectionState::Disconnected) { return; }
        if (data.Size() > m_cfg.maxMessageBytes) {
            DRACONIC_LOG_WARNING(u8"Net", u8"reliable: message {} B exceeds maxMessageBytes {} - dropped (no fragmentation yet)",
                                 data.Size(), m_cfg.maxMessageBytes);
            return;
        }
        OutMessage msg;
        msg.channel = channel;
        msg.reliable = (reliability == Reliability::ReliableOrdered);
        msg.data.Resize(data.Size());
        if (data.Size() > 0) { MemCopy(msg.data.Data(), data.Data(), data.Size()); }
        if (msg.reliable) {
            msg.id = c->nextOutReliableId++;
            c->unackedReliable.PushBack(static_cast<OutMessage&&>(msg));
        } else {
            c->pendingUnreliable.PushBack(static_cast<OutMessage&&>(msg));
        }
    }

    void Disconnect(PeerId peer) override {
        Connection* c = FindByPeer(peer);
        if (c == nullptr || c->state == ConnectionState::Disconnected) { return; }
        SendControl(*c, PacketType::Disconnect);
        c->state = ConnectionState::Disconnected;
    }

    [[nodiscard]] bool Poll(NetEvent& out) override {
        if (m_eventHead >= m_events.Size()) { return false; }
        out = static_cast<NetEvent&&>(m_events[m_eventHead++]);
        if (m_eventHead >= m_events.Size()) { m_events.Clear(); m_eventHead = 0; }
        return true;
    }

    void Update(f32 deltaMs) override {
        m_nowMs += static_cast<f64>(deltaMs);
        ReceiveAll();
        for (Connection& c : m_connections) { UpdateConnection(c); }
    }

    [[nodiscard]] TransportStats Stats(PeerId peer) const override {
        TransportStats s;
        if (const Connection* c = FindByPeer(peer)) { s.rttMs = c->rttMs; s.sentBytes = c->sentBytes; s.recvBytes = c->recvBytes; }
        return s;
    }

private:
    enum class PacketType : u8 { ConnectRequest = 0, ConnectAccept = 1, Data = 2, Disconnect = 3 };

    struct OutMessage { u8 channel = 0; bool reliable = false; u16 id = 0; Array<byte> data; };
    struct SentPacket { u16 seq = 0; f64 sendTimeMs = 0.0; bool acked = false; Array<u16> reliableIds; };
    struct BufferedIn { u16 id = 0; u8 channel = 0; Array<byte> data; };

    struct Connection {
        PeerId           peer = kInvalidPeer;
        DatagramEndpoint remote;
        ConnectionState  state = ConnectionState::Connecting;
        // outgoing sequencing / ack tracking
        u16 localSeq = 0;
        u16 remoteSeq = 0;           // highest received Data seq
        u32 receivedBits = 0;        // history of the 32 packets before remoteSeq
        bool anyReceived = false;
        Array<SentPacket> sentWindow;   // recent sent Data packets (for acks + RTT)
        // reliable messages
        u16 nextOutReliableId = 0;
        Array<OutMessage> unackedReliable;    // resent every packet until acked
        Array<OutMessage> pendingUnreliable;  // sent once, then cleared
        u16 nextInReliableId = 0;             // next ordered id to release
        Array<BufferedIn> reorderBuffer;      // received reliable ids ahead of nextInReliableId
        // timing
        f64 rttMs = 0.0;
        f64 lastRecvMs = 0.0;
        f64 lastSendMs = -1000.0;
        f64 lastConnectSendMs = -1000.0;
        bool ackPending = false;     // received data since our last send => ack promptly
        u32 sentBytes = 0, recvBytes = 0;
    };

    // seq1 is strictly newer than seq2 under u16 wraparound.
    static bool SeqGreater(u16 s1, u16 s2) noexcept {
        return ((s1 > s2) && (static_cast<u32>(s1 - s2) <= 0x8000u))
            || ((s1 < s2) && (static_cast<u32>(s2 - s1) > 0x8000u));
    }

    Connection& OpenConnection(const DatagramEndpoint& remote) {
        for (Connection& c : m_connections) { if (c.remote == remote) { return c; } }
        Connection c;
        c.peer = m_nextPeer++;
        c.remote = remote;
        c.lastRecvMs = m_nowMs;
        m_connections.PushBack(static_cast<Connection&&>(c));
        return m_connections[m_connections.Size() - 1];
    }
    [[nodiscard]] Connection* FindByPeer(PeerId peer) {
        for (Connection& c : m_connections) { if (c.peer == peer) { return &c; } }
        return nullptr;
    }
    [[nodiscard]] const Connection* FindByPeer(PeerId peer) const {
        for (const Connection& c : m_connections) { if (c.peer == peer) { return &c; } }
        return nullptr;
    }
    [[nodiscard]] Connection* FindByRemote(const DatagramEndpoint& r) {
        for (Connection& c : m_connections) { if (c.remote == r) { return &c; } }
        return nullptr;
    }

    void PushEvent(NetEventKind kind, PeerId peer, u8 channel, Array<byte>&& payload) {
        NetEvent ev; ev.kind = kind; ev.peer = peer; ev.channel = channel;
        ev.payload = static_cast<Array<byte>&&>(payload);
        m_events.PushBack(static_cast<NetEvent&&>(ev));
    }

    void RawSend(Connection& c, BitWriter& w) {
        Span<const byte> bytes = w.Data();
        c.sentBytes += static_cast<u32>(bytes.Size());
        m_socket->Send(c.remote, bytes);
        c.lastSendMs = m_nowMs;
    }

    void SendConnectRequest(Connection& c) {
        BitWriter w; w.WriteU16(m_cfg.protocolId); w.WriteU8(static_cast<u8>(PacketType::ConnectRequest));
        RawSend(c, w);
        c.lastConnectSendMs = m_nowMs;
    }
    void SendControl(Connection& c, PacketType type) {
        BitWriter w; w.WriteU16(m_cfg.protocolId); w.WriteU8(static_cast<u8>(type));
        RawSend(c, w);
    }

    // Build + send one Data packet: header + as many unacked-reliable then pending-unreliable
    // messages as fit the budget. Records the sent packet for ack/RTT.
    void SendDataPacket(Connection& c) {
        BitWriter w;
        w.WriteU16(m_cfg.protocolId);
        w.WriteU8(static_cast<u8>(PacketType::Data));
        const u16 seq = c.localSeq++;
        w.WriteU16(seq);
        w.WriteU16(c.remoteSeq);
        w.WriteU32(c.receivedBits);

        // Collect messages within the byte budget.
        SentPacket rec; rec.seq = seq; rec.sendTimeMs = m_nowMs;
        Array<const OutMessage*> chosen;
        usize budget = m_cfg.maxPacketBytes;
        for (const OutMessage& m : c.unackedReliable) {
            if (m.data.Size() + 8u > budget) { break; }
            chosen.PushBack(&m); budget -= (m.data.Size() + 8u);
            rec.reliableIds.PushBack(m.id);
        }
        for (const OutMessage& m : c.pendingUnreliable) {
            if (m.data.Size() + 8u > budget) { break; }
            chosen.PushBack(&m); budget -= (m.data.Size() + 8u);
        }
        w.WriteVarU32(static_cast<u32>(chosen.Size()));
        for (const OutMessage* m : chosen) {
            w.WriteU8(m->channel);
            w.WriteBool(m->reliable);
            if (m->reliable) { w.WriteU16(m->id); }
            w.WriteVarU32(static_cast<u32>(m->data.Size()));
            w.WriteBytes(Span<const byte>(m->data.Data(), m->data.Size()));
        }
        c.pendingUnreliable.Clear();   // unreliable: sent once
        RecordSent(c, static_cast<SentPacket&&>(rec));
        RawSend(c, w);
    }

    void RecordSent(Connection& c, SentPacket&& rec) {
        c.sentWindow.PushBack(static_cast<SentPacket&&>(rec));
        while (c.sentWindow.Size() > 256u) { c.sentWindow.RemoveAt(0); }
    }

    void UpdateConnection(Connection& c) {
        if (c.state == ConnectionState::Disconnected) { return; }
        if (m_nowMs - c.lastRecvMs > static_cast<f64>(m_cfg.timeoutMs)) {
            c.state = ConnectionState::Disconnected;
            PushEvent(NetEventKind::Disconnected, c.peer, 0, {});
            return;
        }
        if (c.state == ConnectionState::Connecting) {
            if (m_nowMs - c.lastConnectSendMs >= static_cast<f64>(m_cfg.connectResendMs)) { SendConnectRequest(c); }
            return;
        }
        // Connected: send a Data packet when there's something to (re)send, an ack is owed, or a
        // keepalive is due.
        const bool haveWork = !c.unackedReliable.IsEmpty() || !c.pendingUnreliable.IsEmpty();
        const bool keepAliveDue = (m_nowMs - c.lastSendMs) >= static_cast<f64>(m_cfg.keepAliveMs);
        if (haveWork || c.ackPending || keepAliveDue) { c.ackPending = false; SendDataPacket(c); }
    }

    void ReceiveAll() {
        DatagramEndpoint from;
        Array<byte> data;
        while (m_socket->Receive(from, data)) {
            HandlePacket(from, data.AsSpan());
        }
    }

    void HandlePacket(const DatagramEndpoint& from, Span<const byte> bytes) {
        BitReader r(bytes);
        const u16 protocol = r.ReadU16();
        const u8 typeRaw = r.ReadU8();
        if (!r.Ok() || protocol != m_cfg.protocolId) { return; }   // foreign/corrupt
        const PacketType type = static_cast<PacketType>(typeRaw);

        Connection* c = FindByRemote(from);
        switch (type) {
            case PacketType::ConnectRequest: {
                if (!m_accepting) { return; }
                if (c == nullptr) {
                    Connection& nc = OpenConnection(from);
                    nc.state = ConnectionState::Connected;
                    nc.lastRecvMs = m_nowMs;
                    SendControl(nc, PacketType::ConnectAccept);
                    PushEvent(NetEventKind::Connected, nc.peer, 0, {});
                } else {
                    c->lastRecvMs = m_nowMs;
                    SendControl(*c, PacketType::ConnectAccept);   // re-accept (lost accept)
                }
                return;
            }
            case PacketType::ConnectAccept: {
                if (c == nullptr) { return; }
                c->lastRecvMs = m_nowMs;
                if (c->state == ConnectionState::Connecting) {
                    c->state = ConnectionState::Connected;
                    PushEvent(NetEventKind::Connected, c->peer, 0, {});
                }
                return;
            }
            case PacketType::Disconnect: {
                if (c != nullptr && c->state != ConnectionState::Disconnected) {
                    c->state = ConnectionState::Disconnected;
                    PushEvent(NetEventKind::Disconnected, c->peer, 0, {});
                }
                return;
            }
            case PacketType::Data: break;
        }
        if (c == nullptr || c->state == ConnectionState::Disconnected) { return; }
        c->recvBytes += static_cast<u32>(bytes.Size());
        c->lastRecvMs = m_nowMs;
        c->ackPending = true;   // owe the sender an ack for this Data packet

        const u16 seq = r.ReadU16();
        const u16 ack = r.ReadU16();
        const u32 ackBits = r.ReadU32();
        if (!r.Ok()) { return; }
        ProcessAcks(*c, ack, ackBits);
        UpdateReceivedhistory(*c, seq);

        const u32 count = r.ReadVarU32();
        for (u32 i = 0; i < count && r.Ok(); ++i) {
            const u8 channel = r.ReadU8();
            const bool reliable = r.ReadBool();
            u16 id = 0;
            if (reliable) { id = r.ReadU16(); }
            const u32 len = r.ReadVarU32();
            if (!r.Ok() || len > m_cfg.maxMessageBytes) { return; }
            Array<byte> payload; payload.Resize(len);
            if (len > 0) { r.ReadBytes(Span<byte>(payload.Data(), len)); }
            if (!r.Ok()) { return; }
            if (reliable) { DeliverReliable(*c, id, channel, static_cast<Array<byte>&&>(payload)); }
            else { PushEvent(NetEventKind::Received, c->peer, channel, static_cast<Array<byte>&&>(payload)); }
        }
    }

    // Mark sent packets acked (by ack + the 32-bit history), ack their reliable messages, sample RTT.
    void ProcessAcks(Connection& c, u16 ack, u32 ackBits) {
        AckOne(c, ack);
        for (u32 bit = 0; bit < 32; ++bit) {
            if (ackBits & (1u << bit)) { AckOne(c, static_cast<u16>(ack - (bit + 1))); }
        }
    }
    void AckOne(Connection& c, u16 seq) {
        for (SentPacket& p : c.sentWindow) {
            if (p.seq == seq && !p.acked) {
                p.acked = true;
                const f64 sample = m_nowMs - p.sendTimeMs;
                c.rttMs = (c.rttMs <= 0.0) ? sample : (c.rttMs * 0.9 + sample * 0.1);
                for (u16 mid : p.reliableIds) { AckReliableMessage(c, mid); }
                return;
            }
        }
    }
    void AckReliableMessage(Connection& c, u16 id) {
        for (usize i = 0; i < c.unackedReliable.Size(); ++i) {
            if (c.unackedReliable[i].id == id) { c.unackedReliable.RemoveAt(i); return; }
        }
    }

    void UpdateReceivedhistory(Connection& c, u16 seq) {
        if (!c.anyReceived) { c.anyReceived = true; c.remoteSeq = seq; c.receivedBits = 0; return; }
        if (SeqGreater(seq, c.remoteSeq)) {
            const u16 shift = static_cast<u16>(seq - c.remoteSeq);
            c.receivedBits = (shift >= 32) ? 0u : ((c.receivedBits << shift) | (1u << (shift - 1)));
            c.remoteSeq = seq;
        } else {
            const u16 diff = static_cast<u16>(c.remoteSeq - seq);
            if (diff >= 1 && diff <= 32) { c.receivedBits |= (1u << (diff - 1)); }
        }
    }

    // Ordered reliable delivery: release in id order, buffering ids that arrive early, dropping dups.
    void DeliverReliable(Connection& c, u16 id, u8 channel, Array<byte>&& payload) {
        if (SeqGreater(c.nextInReliableId, id) || id == static_cast<u16>(c.nextInReliableId - 1)) {
            return;   // already delivered (id < nextIn) - a duplicate resend
        }
        if (id == c.nextInReliableId) {
            PushEvent(NetEventKind::Received, c.peer, channel, static_cast<Array<byte>&&>(payload));
            c.nextInReliableId++;
            // release any buffered consecutive ids
            for (;;) {
                bool released = false;
                for (usize i = 0; i < c.reorderBuffer.Size(); ++i) {
                    if (c.reorderBuffer[i].id == c.nextInReliableId) {
                        PushEvent(NetEventKind::Received, c.peer, c.reorderBuffer[i].channel,
                                  static_cast<Array<byte>&&>(c.reorderBuffer[i].data));
                        c.reorderBuffer.RemoveAt(i);
                        c.nextInReliableId++;
                        released = true;
                        break;
                    }
                }
                if (!released) { break; }
            }
        } else {
            // early arrival - buffer unless already buffered
            for (const BufferedIn& b : c.reorderBuffer) { if (b.id == id) { return; } }
            BufferedIn b; b.id = id; b.channel = channel; b.data = static_cast<Array<byte>&&>(payload);
            c.reorderBuffer.PushBack(static_cast<BufferedIn&&>(b));
        }
    }

    IDatagramSocket* m_socket;
    ReliableConfig   m_cfg;
    bool             m_accepting = false;
    f64              m_nowMs = 0.0;
    PeerId           m_nextPeer = 1;
    Array<Connection> m_connections;
    Array<NetEvent>  m_events;
    usize            m_eventHead = 0;
};

}
