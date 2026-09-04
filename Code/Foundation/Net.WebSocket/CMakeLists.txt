# Foundation::Net.WebSocket - `foundation.net.websocket`: the native WebSocket accept path
# (RFC 6455 handshake through foundation.http + frame codec + server gateway) and the hybrid
# UDP+WS datagram socket, so a desktop host accepts browser clients on the unchanged net stack.

util_add_engine_library(Net.WebSocket ALIAS Foundation::Net.WebSocket)
target_link_libraries(Net.WebSocket PUBLIC Foundation::Core Foundation::Net Foundation::Http Foundation::Policy)

target_sources(Net.WebSocket
    PRIVATE
        WebSocketGatewayImpl.cpp
    PUBLIC
        FILE_SET net_websocket_modules TYPE CXX_MODULES FILES
            WebSocketGateway.cppm)
