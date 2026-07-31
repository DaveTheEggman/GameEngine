# Draconic::EditorGeneric - `draconic.editor.generic`: the serialize-driven FALLBACK asset
# page (every asset without a bespoke page). Linked by the editor EXECUTABLE only.
add_library(Draconic.Editor.Generic STATIC)
add_library(Draconic::EditorGeneric ALIAS Draconic.Editor.Generic)

target_link_libraries(Draconic.Editor.Generic
    PUBLIC
        Draconic::Core Draconic::Content
        Draconic::UI Draconic::UIToolkit
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(Draconic.Editor.Generic
    PRIVATE
        AssetFormPageImpl.cpp
    PUBLIC
        FILE_SET editorgeneric_modules TYPE CXX_MODULES FILES
            AssetFormPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Generic.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
