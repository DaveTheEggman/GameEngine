# Draconic::EditorTexture - `draconic.editor.texture`: the TextureAsset import-settings +
# preview page + factory. Linked by the editor EXECUTABLE only.
add_library(DraconicEditorTexture STATIC)
add_library(Draconic::EditorTexture ALIAS DraconicEditorTexture)

target_link_libraries(DraconicEditorTexture
    PUBLIC
        Draconic::Core Draconic::Content
        Draconic::Image Draconic::ImageIO
        Draconic::Texture Draconic::TextureEditor
        Draconic::UI Draconic::UIToolkit
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(DraconicEditorTexture
    PRIVATE
        TextureEditorPageImpl.cpp
    PUBLIC
        FILE_SET editortexture_modules TYPE CXX_MODULES FILES
            TextureEditorPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
