# Foundation::Http - `foundation.http`: a deliberately small HTTP/1.1 layer over the Core/System
# TCP sockets (via Foundation::Net's TcpSocket/TcpListener). Consumers: the MCP streamable-HTTP
# host, the WebSocket upgrade path (web networking - a WS connection begins as an HTTP
# request), loopback tooling/tests. One request per connection (Connection: close) + SSE streams;
# no TLS (localhost trust domain).

add_library(Http STATIC)
add_library(Foundation::Http ALIAS Http)

target_link_libraries(Http PUBLIC Foundation::Net Foundation::Core Foundation::Policy)

target_sources(Http
    PRIVATE
        HttpImpl.cpp
    PUBLIC
        FILE_SET http_modules TYPE CXX_MODULES FILES
            Http.cppm)
