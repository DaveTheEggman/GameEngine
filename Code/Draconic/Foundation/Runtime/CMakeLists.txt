# Foundation::Runtime - the `foundation.runtime` module (Context + Subsystems).

add_library(Runtime STATIC)
add_library(Foundation::Runtime ALIAS Runtime)

target_link_libraries(Runtime PUBLIC Foundation::Core Foundation::Policy)

target_sources(Runtime
    PUBLIC
        FILE_SET runtime_modules TYPE CXX_MODULES FILES
            RuntimeModule.cppm
            Subsystem.cppm
            RuntimeContext.cppm
            Plugin.cppm
            PluginHost.cppm)
