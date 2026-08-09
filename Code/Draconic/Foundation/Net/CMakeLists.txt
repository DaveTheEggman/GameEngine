# Foundation::Net - `draconic.net` (real-time game networking: wire format + INetTransport with
# swappable backends - reliable-UDP (ours), loopback/sim, later websocket/webrtc - + session,
# reliability, RPC, replication). Over Core (+ Core/System sockets, later). See
# docs/design/networking.md.

add_library(Net STATIC)
add_library(Foundation::Net ALIAS Net)

target_link_libraries(Net PUBLIC Foundation::Core Foundation::Policy)

target_sources(Net
    PUBLIC
        FILE_SET net_modules TYPE CXX_MODULES FILES
            WireStream.cppm
            NetTransport.cppm
            Datagram.cppm
            Reliable.cppm
            NetSession.cppm
            RpcTable.cppm
            UdpSocket.cppm
            TcpSocket.cppm
            NetModule.cppm)

