# Draconic::Net.Manager - `draconic.net.manager`: a networked endpoint owned per running game
# (NetworkManager owns a live NetSession + RpcTable, driven each fixed step) + the `Net` script
# facade (Wren + AngelScript). Over draconic.net + draconic.script. See docs/design/networking.md §6.

add_library(Draconic.Net.Manager STATIC)
add_library(Draconic::Net.Manager ALIAS Draconic.Net.Manager)

target_link_libraries(Draconic.Net.Manager PUBLIC Draconic::Core Draconic::Net Draconic::Net.Replication Draconic::Scene Draconic::Script Draconic::Script.Facades Draconic::Policy)

target_sources(Draconic.Net.Manager
    PUBLIC
        FILE_SET net_manager_modules TYPE CXX_MODULES FILES
            NetworkManager.cppm
    PRIVATE
        NetworkManagerImpl.cpp)

# The tests exercise the Net facade on BOTH script backends, so they need ScriptAngelScript
# (gated off for web, where AngelScript is not yet wired). TARGET guard is order-safe - Script
# configures before Net.
if(DRACONIC_BUILD_TESTS AND TARGET Draconic::Script.AngelScript)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Net.Manager.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
