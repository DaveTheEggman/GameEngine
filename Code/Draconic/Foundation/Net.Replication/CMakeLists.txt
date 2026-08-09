# Foundation::Net.Replication - `draconic.net.replication`: the StateReplication foundation - a stable
# NetworkId + authority, and the REFLECTION-DRIVEN field codec (marked properties -> :wire, no
# hand-written per-component net code). Over Core (reflection) + Net (wire). Snapshot/delta/spawn/
# relevancy stack on top in later slices. See docs/design/networking.md §5, §7 P2.

add_library(Net.Replication STATIC)
add_library(Foundation::Net.Replication ALIAS Net.Replication)

target_link_libraries(Net.Replication PUBLIC Foundation::Core Foundation::Net Foundation::Scene Foundation::Resource Foundation::Script.Facades Foundation::Policy)

target_sources(Net.Replication
    PUBLIC
        FILE_SET net_replication_modules TYPE CXX_MODULES FILES
            Replication.cppm
    PRIVATE
        ReplicationImpl.cpp
        Interpolation.cpp)

