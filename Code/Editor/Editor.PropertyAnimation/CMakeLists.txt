# Editor::PropertyAnimation - `editor.propertyanimation`: the PropertyAnimationClipAsset editing page
# + factory + New Asset creator. Linked by the editor EXECUTABLE only.
add_library(Editor.PropertyAnimation STATIC)
add_library(Editor::PropertyAnimation ALIAS Editor.PropertyAnimation)

target_link_libraries(Editor.PropertyAnimation
    PUBLIC
        Foundation::Core Foundation::Content Foundation::Scene Foundation::Render
        Foundation::Runtime Foundation::Runtime.Client
        Foundation::PropertyAnimation Foundation::PropertyAnimation.Resource Pipeline::PropertyAnimation
        Foundation::UI Foundation::UI.Runtime Foundation::UI.Toolkit
        Editor::Core Editor::App Editor::ViewportTools Foundation::Policy)

target_sources(Editor.PropertyAnimation
    PRIVATE
        ClipEditorViewImpl.cpp
        PropertyAnimationToolImpl.cpp
        PropertyAnimationClipPageImpl.cpp
    PUBLIC
        FILE_SET editorpropanim_modules TYPE CXX_MODULES FILES
            ClipEditorView.cppm
            PropertyAnimationTool.cppm
            PropertyAnimationClipPage.cppm)
