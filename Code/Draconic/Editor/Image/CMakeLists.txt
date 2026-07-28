# Draconic::EditorImage - `draconic.editor.image`: the ImageAsset inspect + color-space
# page + factory (the Texture page's source-side sibling). Linked by the editor EXECUTABLE only.
add_library(DraconicEditorImage STATIC)
add_library(Draconic::EditorImage ALIAS DraconicEditorImage)

target_link_libraries(DraconicEditorImage
    PUBLIC
        Draconic::Core Draconic::Content
        Draconic::Image Draconic::ImageIO Draconic::ImageEditor
        Draconic::UI Draconic::UIToolkit
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(DraconicEditorImage
    PRIVATE
        ImageEditorPageImpl.cpp
    PUBLIC
        FILE_SET editorimage_modules TYPE CXX_MODULES FILES
            ImageEditorPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
