# Draconic::NetManager - `draconic.net.manager`: a networked endpoint owned per running game
# (NetworkManager owns a live NetSession + RpcTable, driven each fixed step) + the `Net` script
# facade (Wren + AngelScript). Over draconic.net + draconic.script. See docs/design/networking.md §6.

add_library(DraconicNetManager STATIC)
add_library(Draconic::NetManager ALIAS DraconicNetManager)

target_link_libraries(DraconicNetManager PUBLIC Draconic::Core Draconic::Net Draconic::NetReplication Draconic::Scene Draconic::Script Draconic::ScriptFacades Draconic::Policy)

target_sources(DraconicNetManager
    PUBLIC
        FILE_SET net_manager_modules TYPE CXX_MODULES FILES
            NetworkManager.cppm
    PRIVATE
        NetworkManagerImpl.cpp)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
