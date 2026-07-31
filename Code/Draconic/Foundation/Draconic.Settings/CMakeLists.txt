# Draconic::Settings - `draconic.settings` (typed, versioned, backend-agnostic settings store).
# Sits just above Core so both the runtime and the editor can use it. Depends only on Core.
# See docs/design/settings.md.

add_library(Draconic.Settings STATIC)
add_library(Draconic::Settings ALIAS Draconic.Settings)

target_link_libraries(Draconic.Settings PUBLIC Draconic::Core Draconic::Policy)

target_sources(Draconic.Settings
    PUBLIC
        FILE_SET settings_modules TYPE CXX_MODULES FILES
            Settings.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
