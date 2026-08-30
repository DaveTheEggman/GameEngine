// SPDX-License-Identifier: MIT
// Copyright (c) 2026-Present Robert Campbell

/// Foundation::Net - the `foundation.net` module.
///
/// Real-time game networking: the wire format, a transport abstraction
/// (INetTransport) with swappable backends - reliable-UDP (ours), an in-memory loopback/sim for
/// deterministic tests, and websocket/webrtc - plus session, reliability, RPC, and
/// replication built on top. Sits over the Core/System socket primitives; HTTP is a separate
/// sibling module (foundation.http), not part of this one.
///
/// The wire layer, the transport seam, and the loopback/sim transport are all headlessly
/// testable with no OS sockets.

export module foundation.net;

export import :wire;
export import :transport;
export import :datagram;
export import :reliable;
export import :session;
export import :rpc;
export import :udp_socket;
export import :tcp_socket;
