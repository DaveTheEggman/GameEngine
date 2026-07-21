# Draconic::NetReplication - `draconic.net.replication`: the StateReplication foundation - a stable
# NetworkId + authority, and the REFLECTION-DRIVEN field codec (marked properties -> :wire, no
# hand-written per-component net code). Over Core (reflection) + Net (wire). Snapshot/delta/spawn/
# relevancy stack on top in later slices. See docs/design/networking.md §5, §7 P2.

add_library(DraconicNetReplication STATIC)
add_library(Draconic::NetReplication ALIAS DraconicNetReplication)

target_link_libraries(DraconicNetReplication PUBLIC Draconic::Core Draconic::Net Draconic::Scene Draconic::Resource Draconic::Policy)

target_sources(DraconicNetReplication
    PUBLIC
        FILE_SET net_replication_modules TYPE CXX_MODULES FILES
            Replication.cppm
    PRIVATE
        ReplicationImpl.cpp)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
