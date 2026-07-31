# Draconic::EditorTexture - `draconic.editor.texture`: the TextureAsset import-settings +
# preview page + factory. Linked by the editor EXECUTABLE only.
add_library(Draconic.Editor.Texture STATIC)
add_library(Draconic::EditorTexture ALIAS Draconic.Editor.Texture)

target_link_libraries(Draconic.Editor.Texture
    PUBLIC
        Draconic::Core Draconic::Content
        Draconic::Image Draconic::ImageIO
        Draconic::Texture Draconic::TextureEditor
        Draconic::UI Draconic::UIToolkit
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(Draconic.Editor.Texture
    PRIVATE
        TextureEditorPageImpl.cpp
    PUBLIC
        FILE_SET editortexture_modules TYPE CXX_MODULES FILES
            TextureEditorPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Texture.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
