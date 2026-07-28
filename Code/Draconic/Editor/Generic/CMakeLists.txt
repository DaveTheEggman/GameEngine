# Draconic::EditorGeneric - `draconic.editor.generic`: the serialize-driven FALLBACK asset
# page (every asset without a bespoke page). Linked by the editor EXECUTABLE only.
add_library(DraconicEditorGeneric STATIC)
add_library(Draconic::EditorGeneric ALIAS DraconicEditorGeneric)

target_link_libraries(DraconicEditorGeneric
    PUBLIC
        Draconic::Core Draconic::Content
        Draconic::UI Draconic::UIToolkit
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(DraconicEditorGeneric
    PRIVATE
        AssetFormPageImpl.cpp
    PUBLIC
        FILE_SET editorgeneric_modules TYPE CXX_MODULES FILES
            AssetFormPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
