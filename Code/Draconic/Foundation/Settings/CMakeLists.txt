# Foundation::Settings - `draconic.settings` (typed, versioned, backend-agnostic settings store).
# Sits just above Core so both the runtime and the editor can use it. Depends only on Core.
# See docs/design/settings.md.

add_library(Settings STATIC)
add_library(Foundation::Settings ALIAS Settings)

target_link_libraries(Settings PUBLIC Foundation::Core Foundation::Policy)

target_sources(Settings
    PUBLIC
        FILE_SET settings_modules TYPE CXX_MODULES FILES
            Settings.cppm)

