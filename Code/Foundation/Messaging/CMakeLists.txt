# Foundation::Messaging - `foundation.messaging`: a native, name-keyed, deferred EventBus (over Core).
# Separate from foundation.scene so a run scope (a scene, or a whole run) can own one
# and share it. A LEAF under Core - no scene/script dependency.

add_library(Messaging STATIC)
add_library(Foundation::Messaging ALIAS Messaging)

target_link_libraries(Messaging PUBLIC Foundation::Core Foundation::Policy)

target_sources(Messaging
    PUBLIC
        FILE_SET messaging_modules TYPE CXX_MODULES FILES
            EventBus.cppm)
