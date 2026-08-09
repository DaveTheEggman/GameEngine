# Foundation::Runtime.Client - `draconic.runtime.client` (the Application base class).
# Loop-agnostic: the desktop runner (draconic.runtime.desktop) drives Start/Tick/Stop against the shell.

add_library(Runtime.Client STATIC)
add_library(Foundation::Runtime.Client ALIAS Runtime.Client)

target_link_libraries(Runtime.Client
    PUBLIC Foundation::Runtime Foundation::Shell Foundation::Graphics Foundation::Profiler Foundation::Policy)

target_sources(Runtime.Client
    PUBLIC
        FILE_SET runtime_client_modules TYPE CXX_MODULES FILES
            Application.cppm
            EmbeddedHost.cppm
            ApplicationHost.cppm)


# Build stamp: regenerated EVERY build (content-compared, so no-op builds stay no-op) and
# compiled into Runtime.Client - every app that uses APP_MAIN logs it at startup.
# The definitive "which build is this" answer, most valuable on web where .wasm caches hard.
set(BUILDSYSTEM_BUILD_STAMP_CPP "${CMAKE_CURRENT_BINARY_DIR}/BuildStamp.cpp")
execute_process(COMMAND ${CMAKE_COMMAND}
    -DOUT=${BUILDSYSTEM_BUILD_STAMP_CPP} -DSRC_ROOT=${CMAKE_SOURCE_DIR}
    -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildStamp.cmake) # exists at configure
add_custom_target(BuildStamp.Tick
    COMMAND ${CMAKE_COMMAND}
        -DOUT=${BUILDSYSTEM_BUILD_STAMP_CPP} -DSRC_ROOT=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildStamp.cmake
    BYPRODUCTS ${BUILDSYSTEM_BUILD_STAMP_CPP}
    COMMENT "Refreshing build stamp")
add_dependencies(Runtime.Client BuildStamp.Tick)
target_sources(Runtime.Client PRIVATE ${BUILDSYSTEM_BUILD_STAMP_CPP})
