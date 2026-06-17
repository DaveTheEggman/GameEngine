# Raptor::RuntimePlatform — `raptor.runtime.platform` (IPlatform/IWindow, the raw
# platform service). Interfaces only; native + null backends implement them.

add_library(RaptorRuntimePlatform STATIC)
add_library(Raptor::RuntimePlatform ALIAS RaptorRuntimePlatform)

target_link_libraries(RaptorRuntimePlatform PUBLIC Raptor::Core Raptor::Policy)

target_sources(RaptorRuntimePlatform
    PUBLIC
        FILE_SET runtime_platform_modules TYPE CXX_MODULES FILES
            Platform.cppm)

# Null backend only needs the interface. The Desktop backend depends on the
# client (its runner drives Application), so it is added from the parent after
# Client is defined.
add_subdirectory(Null)
