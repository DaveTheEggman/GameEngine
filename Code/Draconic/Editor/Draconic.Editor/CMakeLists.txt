# Draconic::Editor - `draconic.editor` (asset-pipeline authoring base: Asset + builders).
# Tooling only - the runtime never links this.

add_library(Draconic.Editor STATIC)
add_library(Draconic::Editor ALIAS Draconic.Editor)

target_link_libraries(Draconic.Editor PUBLIC Draconic::Core Draconic::Content Draconic::VFS Draconic::Policy)

target_sources(Draconic.Editor
    PUBLIC
        FILE_SET editor_modules TYPE CXX_MODULES FILES
            Asset.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
