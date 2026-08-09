# Editor::Image - `draconic.editor.image`: the ImageAsset inspect + color-space
# page + factory (the Texture page's source-side sibling). Linked by the editor EXECUTABLE only.
add_library(Editor.Image STATIC)
add_library(Editor::Image ALIAS Editor.Image)

target_link_libraries(Editor.Image
    PUBLIC
        Foundation::Core Foundation::Content
        Foundation::Image Foundation::Image.IO Pipeline::Image
        Foundation::UI Foundation::UI.Toolkit
        Editor::Core Editor::App Foundation::Policy)

target_sources(Editor.Image
    PRIVATE
        ImageEditorPageImpl.cpp
    PUBLIC
        FILE_SET editorimage_modules TYPE CXX_MODULES FILES
            ImageEditorPage.cppm)

