# Raptor::Editor — `raptor.editor` (asset-pipeline authoring base: Asset + builders).
# Tooling only — the runtime never links this.

add_library(RaptorEditor STATIC)
add_library(Raptor::Editor ALIAS RaptorEditor)

target_link_libraries(RaptorEditor PUBLIC Raptor::Core Raptor::Content Raptor::Policy)

target_sources(RaptorEditor
    PUBLIC
        FILE_SET editor_modules TYPE CXX_MODULES FILES
            Asset.cppm)

if(RAPTOR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
