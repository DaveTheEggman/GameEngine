# Draconic::Editor — `draconic.editor` (asset-pipeline authoring base: Asset + builders).
# Tooling only — the runtime never links this.

add_library(DraconicEditor STATIC)
add_library(Draconic::Editor ALIAS DraconicEditor)

target_link_libraries(DraconicEditor PUBLIC Draconic::Core Draconic::Content Draconic::Policy)

target_sources(DraconicEditor
    PUBLIC
        FILE_SET editor_modules TYPE CXX_MODULES FILES
            Asset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
