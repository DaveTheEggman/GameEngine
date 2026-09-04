# Foundation::Settings - `foundation.settings` (typed, versioned, backend-agnostic settings store).
# Sits just above Core so both the runtime and the editor can use it. Depends only on Core.

util_add_engine_library(Settings ALIAS Foundation::Settings)
target_link_libraries(Settings PUBLIC Foundation::Core Foundation::Policy)

target_sources(Settings
    PUBLIC
        FILE_SET settings_modules TYPE CXX_MODULES FILES
            Settings.cppm)

