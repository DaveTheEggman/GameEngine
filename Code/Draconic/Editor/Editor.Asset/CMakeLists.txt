# Draconic::Editor.Asset - `draconic.editor` (asset-pipeline authoring base: Asset + builders).
# Tooling only - the runtime never links this.

add_library(Draconic.Editor.Asset STATIC)
add_library(Draconic::Editor.Asset ALIAS Draconic.Editor.Asset)

target_link_libraries(Draconic.Editor.Asset PUBLIC Draconic::Core Draconic::Content Draconic::VFS Draconic::Policy)

target_sources(Draconic.Editor.Asset
    PRIVATE
        AssetImpl.cpp
    PUBLIC
        FILE_SET editor_modules TYPE CXX_MODULES FILES
            Asset.cppm)