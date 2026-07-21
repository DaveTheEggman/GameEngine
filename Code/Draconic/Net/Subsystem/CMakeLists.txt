# Draconic::NetSubsystem - `draconic.net.subsystem`: the runtime home for networking (NetSubsystem
# owns a live NetSession + RpcTable, driven each fixed step) + the `Net` script facade (Wren +
# AngelScript). Over draconic.net + draconic.script. See docs/design/networking.md §6.

add_library(DraconicNetSubsystem STATIC)
add_library(Draconic::NetSubsystem ALIAS DraconicNetSubsystem)

target_link_libraries(DraconicNetSubsystem PUBLIC Draconic::Core Draconic::Net Draconic::NetReplication Draconic::Scene Draconic::Script Draconic::ScriptFacades Draconic::Policy)

target_sources(DraconicNetSubsystem
    PUBLIC
        FILE_SET net_subsystem_modules TYPE CXX_MODULES FILES
            Subsystem.cppm
    PRIVATE
        SubsystemImpl.cpp)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
