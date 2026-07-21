# Draconic::Net - `draconic.net` (real-time game networking: wire format + INetTransport with
# swappable backends - reliable-UDP (ours), loopback/sim, later websocket/webrtc - + session,
# reliability, RPC, replication). Over Core (+ Core/System sockets, later). See
# docs/design/networking.md.

add_library(DraconicNet STATIC)
add_library(Draconic::Net ALIAS DraconicNet)

target_link_libraries(DraconicNet PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicNet
    PUBLIC
        FILE_SET net_modules TYPE CXX_MODULES FILES
            WireStream.cppm
            Transport.cppm
            Datagram.cppm
            Reliable.cppm
            Session.cppm
            Rpc.cppm
            UdpSocket.cppm
            TcpSocket.cppm
            NetModule.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
