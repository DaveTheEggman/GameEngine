# Foundation::Net.Manager - `foundation.net.manager`: a networked endpoint owned per running game
# (NetworkManager owns a live NetSession + RpcTable, driven each fixed step) + the `Net` script
# facade (Wren + AngelScript). Over foundation.net + foundation.script. See docs/design/networking.md §6.

add_library(Net.Manager STATIC)
add_library(Foundation::Net.Manager ALIAS Net.Manager)

target_link_libraries(Net.Manager PUBLIC Foundation::Core Foundation::Net Foundation::Net.Replication Foundation::Scene Foundation::Script Foundation::Script.Facades Foundation::Policy)

target_sources(Net.Manager
    PUBLIC
        FILE_SET net_manager_modules TYPE CXX_MODULES FILES
            NetworkManager.cppm
    PRIVATE
        NetworkManagerImpl.cpp)

