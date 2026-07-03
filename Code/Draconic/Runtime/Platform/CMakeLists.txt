# Draconic::RuntimePlatform — `draconic.runtime.platform` (IPlatform/IWindow, the raw
# platform service). Interfaces only; native + null backends implement them.

add_library(DraconicRuntimePlatform STATIC)
add_library(Draconic::RuntimePlatform ALIAS DraconicRuntimePlatform)

target_link_libraries(DraconicRuntimePlatform PUBLIC Draconic::Core Draconic::Policy)

target_sources(DraconicRuntimePlatform
    PUBLIC
        FILE_SET runtime_platform_modules TYPE CXX_MODULES FILES
            InputTypes.cppm
            Input.cppm
            InputSurface.cppm
            Platform.cppm)

# Null backend only needs the interface. The Desktop backend depends on the
# client (its runner drives Application), so it is added from the parent after
# Client is defined.
add_subdirectory(Null)
