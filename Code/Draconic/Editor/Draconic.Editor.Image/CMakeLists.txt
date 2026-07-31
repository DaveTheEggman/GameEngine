# Draconic::EditorImage - `draconic.editor.image`: the ImageAsset inspect + color-space
# page + factory (the Texture page's source-side sibling). Linked by the editor EXECUTABLE only.
add_library(Draconic.Editor.Image STATIC)
add_library(Draconic::EditorImage ALIAS Draconic.Editor.Image)

target_link_libraries(Draconic.Editor.Image
    PUBLIC
        Draconic::Core Draconic::Content
        Draconic::Image Draconic::ImageIO Draconic::ImageEditor
        Draconic::UI Draconic::UIToolkit
        Draconic::EditorCore Draconic::EditorApp Draconic::Policy)

target_sources(Draconic.Editor.Image
    PRIVATE
        ImageEditorPageImpl.cpp
    PUBLIC
        FILE_SET editorimage_modules TYPE CXX_MODULES FILES
            ImageEditorPage.cppm)

if(DRACONIC_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../Draconic.Editor.Image.Tests ${CMAKE_CURRENT_BINARY_DIR}/Tests)
endif()
